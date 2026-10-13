# 0.8.0 release validation

Protocol **256**, replay format **12**, generator version **8**, campaign
payload **4**, database schema **9**.

Tagged `v0.8.0` at `477c9238` and published on 2026-10-13.

## Scope and compatibility

0.8.0 follows 0.7.28 (protocol 253) and carries protocols 254, 255 and 256:

- **Legion formation flyers over the front (protocol 254).** Flyer stations,
  positions and the flyer order point in squads that mix ground units and
  flyers (Legion only; Retail exits first). As the commits record, the seeded
  `--mpai` Inner Circle 300 s hashes were unchanged in both modes (Retail
  `95b05c8290b55317`, Legion `21055e66adec40d6`), since that AI game forms no
  mixed squads; `legion_world_test` (mixedformation, flyerfront) and
  `legion_flyers_test` cover the change.
- **Factory output site admission (protocol 255).** A scripted producer's
  ground output is admitted by the footprint-strict mobile placement test (icd
  401deb stage 2, 507d10), and a refused site waits `rand(15)+7` ticks on the
  hashed `Unit::productionRetryTick`. Retail's GetBuilt release (rally or
  PARK) is documented but not ported. `production_exit` adds 228 cases (every
  shipped ground factory); the `--mpai` hashes were unchanged (no site is
  refused there), and `check-determinism` gave `dcef618cd2e4d558` on the
  landing commit. See `docs/factory-output-2026-10-12.md`.
- **Air transport unload standby (protocol 256).** An air carrier whose unload
  empties its order list takes its default mission in the same call that
  retires VTOL_UNLOAD (icd 4d8450/4d856a), with the step-out controller still
  attached, so VTOL_Standby lands it two ticks on instead of flying it
  1,048-1,496 px along that controller (zonroc, tarship, creaeri);
  `World::standbyEligible` no longer waits for an active retained controller.
  `tools/re/probe_transport_air_unload_default_mission.py` runs the real
  dispatcher and unload. `transport_post_unload` covers every shipped carrier
  (13 standard, 14 Crusades), Retail and Legion, one passenger and a full load
  (204 cases, all 48 air cases failing before), plus 24 queued-order cases. As
  the commit records, ctest on build-o2 passed 552/552, the seeded `--mpai`
  Inner Circle 300 s hashes were unchanged (Retail `95b05c8290b55317`, Legion
  `21055e66adec40d6`; no unload happens there) and `check-determinism` gave
  `dcef618cd2e4d558`. Documented deviation: an ordinary flyer move's detached
  coast still settles before standby.
- **Display-only simulation-adjacent changes.** The Legion walk-animation pace
  runs in the client's display COB VM; the authoritative World VM is untouched
  and the `--mpai` hashes were unchanged.
- **Byte-exact refactors.** One match-setup builder and one GameOptions /
  SlotInfo codec (`match_codec_test` pins goldens captured from the hand-written
  copies; replays and wire frames identical), the live TickBundle decoder kept
  lenient (strict only for replay files), the debug harness moved out of Release,
  and the client's const `world_` view. None changes `kNetVersion` or hashed
  state.
- **Client-only changes:** the information bar's multi-selection strip, group
  totals, squad label and centred status text; the solid green squad marker;
  three-way Texture Filtering; reclaim and Revive picking; override cache
  saving; menu launch-failure routing and dead local-server detection.
- **Cartographer:** symmetry tools, terrain clipboard, grouped section browser,
  wider built-in font and the per-concern source split (headless `--shot`,
  `--save` and `--bundle` outputs byte-identical across the split).
- **Platform and CI:** macOS Developer ID signing, notarization and stapling in
  a separate `sign` job; ccache for Windows, Linux and macOS; superseded cache
  pruning.

Protocol 256 is required for live play. Replays need the exact simulation
protocol, so protocol-253 (0.7.28) recordings are not playable by this build.
Replay format 12, generator version 8, campaign payload 4 and database schema
9 are unchanged since 0.7.28 (checked in source against `v0.7.28`: `git diff`
touches none of `src/net/replayhdr.h`, `src/tnt/`, `src/campaign/`,
`src/net/crusades.h` or the Crusades store).

## Local preparation checks

All checks ran on a clean detached worktree of `origin/main` at
`b1e42380d706dc22f66557f59f9862d1d3f56b42` (protocol **256**, replay format 12).
The version bump lives on the docs branch, so these binaries still call themselves
0.7.28. The owned retail install served as `--data` and `TAK_TEST_DATA`. Toolchains
were GCC 16.2.1, Clang 22.1.8, CMake 4.3.0, Ninja 1.13.2 and Python 3.14.8. Cores
0–3 stay reserved for timing work, so builds and tests ran on cores 4–23.

**Result: every check passed.** There was no crash, desync, sanitizer finding or
stale baseline, so no `release-local-080-baselines` branch was needed. Unlike
0.7.28, no fix was required before tagging. Details follow.

- **Builds.** Release, Debug, optimized Debug (`-O2 -g`), Clang Debug and
  AddressSanitizer all-target builds completed with exit code 0: 585 steps in
  Release and 586 in the Debug-type trees. These were fresh trees, with ccache on
  the GCC builds. They use the static-deps prefix (`third_party/static-deps`) and
  the static Bink/streaming FFmpeg prefix (`third_party/ffmpeg-bink`). Every client
  and server reports `takclient (TAK engine) 0.7.28 (build v0.7.28-49-gb1e42380d706)`
  and `takserver (TAK engine) 0.7.28 (build v0.7.28-49-gb1e42380d706)`. The build
  identity is clean, with no `-dirty` suffix. The server announces `protocol v256`.
- **Full CTest.** There are now 552 registered tests (538 in Release), up from 533.
  160 of them carry the `nightly` label, as in 0.7.28. The 19 new tests are
  `cartographer_headless_ui`, `cartographer_sections_ui`, `cartographer_symmetry_ui`,
  `cartographer_terrain_clipboard_ui`, `gaitpace`, `gaitpace_shipped`,
  `legion_flyerfront`, `local_server`, `match_codec`, `menu_launch_failure`,
  `native_air_transport_animation_pose`, `native_creaeri_landing_pose`,
  `native_lifbird_ambient_pose`, `native_zonroc_landing_pose`, `reclaim_pick`,
  `selportraits`, `session_failure`, `texture_filter` and `transport_post_unload`.
  - **Release: 538/538 passed. Optimized Debug: 552/552 passed.** Both ran the full
    suite, nightly label included. The worktree had an `assets` link to the real
    `assets` directory, so `cobanim` ran its 11 checks (see the 0.7.28 note).
  - **Debug and Clang Debug ran with `-LE nightly`**, as in 0.7.28: at `-O0` some
    nightly tests exceed their timeouts, and the nightly job only runs them at
    `-O2`. **Debug: 392/392 passed. Clang Debug: 392/392 passed.** The two suites
    ran one after the other this time, not at once.
  - **All 67 `legion_check_*` scenario gates passed** in Release and optimized
    Debug against the unchanged `tools/scenarios/baseline.json` (no commit since
    `v0.7.28` touches it). Protocols 254–256 (Legion flyers over the front of mixed
    formations, footprint-strict factory output, air transports landing at the
    drop point) did not move any gated hash. `legion_check.py validate --baseline
    tools/scenarios/baseline.json` reports 0 problems.
  - **Battle cost and naval tests passed.** `legion_cost`,
    `legion_scenario_battle-field-2x500`, `legion_scenario_battle-assault`,
    `naval_ai` and `legion_acceptance_crowdheld_legion` passed in Release and
    optimized Debug, with no ratchet failure.
  - **Slowest tests.** In Release and optimized Debug: `legion_check_pocket-615-open`
    (832.6 s / 867.5 s), `legion_check_tail-corner380` (804.4 s / 842.5 s) and
    `legion_check_pocket-615-wall` (785.3 s / 836.1 s). All are under their 1800 s
    limit. In Debug and Clang Debug the slowest was `legion_w7_awarebig`
    (333.9 s / 328.5 s).
  - **Headroom note.** Under unoptimized Clang, `movement_orders_trolls` took
    278.5 s against its 300 s limit. It passed, but this is the same thin margin
    0.7.28 noted.
- **Focused AddressSanitizer/leak run: 141/141 passed.** The selection is the 122
  tests from 0.7.28 plus the 19 new tests listed above. The run used
  `bash tools/run-ctest-selection.sh` with `SDL_SHUTDOWN_DBUS_ON_QUIT=1`, dummy SDL
  video/audio, `ASAN_OPTIONS=detect_leaks=1` and no suppressions. There were no
  AddressSanitizer, LeakSanitizer or runtime-error reports.
- **Retail group emulation check (formation-air).** I ran
  `python3 -B tools/re/check_retail_group.py` (400 cases by default) against the
  emulated `KINGDOMS.icd`. It passed every section: tick 400, query 4000, check
  2000, formation 2000, vtolcheck 2000 and vtolform 2000. The outcome counts are
  identical to 0.7.28, including vtolcheck: 367 reform, 48 wait and 19 cancel.
- **Determinism.** In `tools/check-determinism.sh`, the GCC and Clang x86-64 builds
  at O0/O2/O3 all agree on `dcef618cd2e4d558`, the same as 0.7.28. The aarch64 legs
  are skipped because target headers are missing; native ARM64 CI remains a separate
  gate. `tools/check-detmath.sh` passes, including the `std::atomic_ref` guard.
- **`--mpai` golden hashes.** All four runs were made twice and produced the same
  hash both times. The client and server logs contain no desync or referee-suspect
  line. The runs used the optimized Debug build, Inner Circle,
  `takserver --local --no-auth --seed 1`, an empty `XDG_DATA_HOME`, and
  `TAK_LEGION=1` for the Legion runs. **Every hash is unchanged from 0.7.28.**
  Protocols 254–256 change hashed state only for flyers in mixed Legion
  formations, crowded factory exits and air-transport unloads. The unchanged
  hashes show that these games never reach those cases.

  | Mode | Duration | Hash (both runs) | 0.7.28 |
  | --- | ---: | --- | --- |
  | Legion | 300 s | `21055e66adec40d6` | `21055e66adec40d6` |
  | Retail | 300 s | `95b05c8290b55317` | `95b05c8290b55317` |
  | Legion | 60 s | `025e4a35abfcd04e` | `025e4a35abfcd04e` |
  | Retail | 60 s | `84fdef37bbb3606e` | `84fdef37bbb3606e` |

  Optimized Debug binary SHA-256:
  - client `3ce6a72f8a27cf44d7aa69a41d626e1166fcdd1b155e90daefc0e62620852c45`
  - server `15cb21e256692ef8b74db6bf69846a63240dfbd36da817b4ef2c7d4893d349bb`
- **Python research suite.** **165 tests** passed, with one skip: the optional corpus
  test, which runs only when `TAK_AUTH_RETAIL_DIR` is set. The suite ran after the
  Release build, so the `build/hpitool` tests ran too. The separate owned-retail
  archive verifier (`TAK_AUTH_RETAIL_DIR`) passed all **6 tests**, including the
  corpus and in-memory negative checks. I took SHA-256 sums of the 16 retail HPI
  archives and `Kingdoms.key` before and after the runs. They are unchanged, and
  identical to the 0.7.28 run.
- **Syntax checks.** All **438** tracked Python files compile. All **21** tracked
  `*.sh` scripts and the extensionless `tools/tak-netem` pass `bash -n`.
- **Notes (informational).**
  - The Release build emits 530 warnings, in the same categories as 0.7.28
    (`-Wmissing-field-initializers`, `-Wdeprecated-declarations`,
    `-Wmisleading-indentation`, `-Wunused-parameter`, `-Wsign-compare`,
    `-Wignored-attributes`) plus one `-Wstringop-overread`. That one is in the
    vendored SQLite amalgamation (`sqlite3Strlen30` inlined into
    `sqlite3ColumnSetColl`), a GCC false positive in third-party code. There is no
    new warning in engine code.
  - `tools/run-ctest-selection.sh` is still tracked as mode 100644, so it must be
    run with `bash`.
  - A worktree outside the main checkout still needs an `assets` link for
    `cobanim`.

Logs: `~/tak-tmp/scratch/release-080/local/` (`cfg-*.log`, `make-*.log`,
`build-status.txt`, `versions.txt`, `toolchains.txt`, `tests-o2.txt`,
`ctest-*.log`, `ctest-status.txt`, `ctest-asan.log`, `asan-list.txt`, `mpai-*.log`,
`mpai-results.txt`, `o2-sha256.txt`, `check-determinism.log`, `check-detmath.log`,
`check_retail_group.log`, `legion-validate.log`, `python-suite.log`,
`verifier.log`, `pycompile.log`, `bashn.log`, `retail-sha-{before,after}.txt`).

## Remote multiplayer sweep

The full 37-scenario table passed for both shipped pathfinding modes, Retail and
Legion, on **2026-10-13 (05:26–06:25 UTC)**. Referees ran on `tak.pgnet.us` and
`vpn3.pgnet.us`, with optimized local clients. Each scenario allowed ten
simulated minutes (18,000 ticks) or ended at an actual result for that seat.
Real seated players provide the client/referee checksum coverage. The two
all-AI spectator scenarios per mode test flow control, not hashes.

The harness picks the mode through the hosting client's room options:
`TAK_LEGION=1` creates a Legion room, and without it the room is Retail. Every
recorded client and referee replay header (73 Retail, 67 + 10 Legion) confirms
the intended mode, replay format 12 and protocol 256. The headers carry engine
version 0.7.28 because the sweep ran on `origin/main` before the 0.8.0 version
bump, which changes only the version string.

| Mode | Completed scenarios | Completed seats | Client-pair checkpoints | Client–referee checkpoints |
| --- | ---: | ---: | ---: | ---: |
| Retail | 37/37 | 52/52 | 11,205 across 22 seat pairs, 0 mismatched | 1,519 across 28 seat/referee comparisons, 0 mismatched |
| Legion | 37/37 (33 first pass + 4 retried) | 52/52 (47 first pass; all 6 seats of the 4 retried cases rerun) | 11,607 across 22 seat pairs, 0 mismatched | 2,023 across 36 seat/referee comparisons, 0 mismatched |

Each mode passed both negative gates before its table started. The referee
reported the planted desync (`client 1 DESYNCED at tick 900`), and the
override-transfer test rejected the incorrect loaded gameplay data
(`verified packs transferred and incorrect loaded gameplay rejected`). The
build-identity gate also passed in each mode, and again before the Legion
retries (`both at v0.7.28-49-gb1e42380d706`). No natural `DESYNCED`,
`REFEREE SUSPECT` or client-reported desync appeared in any case log. The only
`err=desync detected` lines are the planted ones in the validation client logs.

The checkpoint counts compare the per-tick hash records in the seats'
`.takrep` files (every 30 ticks). The client-referee column compares the seats
with referee replays (every 300 ticks). Each format-12 checkpoint carries a
position digest as well as the state hash, and the comparison script checks
both wherever both sides recorded one. All 2,693 referee checkpoints and all
56,765 seated checkpoints carried a digest. Before the counts were trusted, the
script was checked against a copy of one game's replay (`2h-baseline`, Retail)
with one hash bit flipped, and then one digest bit flipped. It reported a
mismatch both times. Referee replays were matched to their cases by the seed in
the replay header as well as by port. The `--only` retry renumbers the ports
from the first port, so a port-only mapping would have paired the retry
replays with the wrong cases. The referee counts are partial: the harness
stops each referee as soon as its clients exit, so only 22 Retail games (20
seated) and 21 + 4 Legion games (20 + 3 seated) wrote a referee replay. The
referee's live check runs on every seated game, whether or not a replay was
written.

**Legion retries.** As in 0.7.25–0.7.28, the dense Ulasem Arena stress cases
ran past the default 900-second real-time limit in the first Legion pass
(`timeout -k 30`, rc=137) without any error or desync. There were four:
`h-stress` (seed 2003), `2h-stress` (seed 2021), `2h-orders-stress`
(seed 2023) and the spectator `w-allai-stress` (seed 2035). The harness labels
these `HIT ... no-completion rc=137` and lists them as NEVER RAN, because a
seat wrote no result line. The referee logs held no desync or suspect report.
In `2h-orders-stress` the host seat concluded at tick 6,811 (8,474 units)
before the deadline, and only the second seat was cut off. All four were rerun
together with `TAK_WALL_TIMEOUT=3600`, the same seeds and the same
simulated-time limit. The rerun started with all three gates, and every seat
passed. All six seats reached 18,000 ticks after about 1,050–1,170 s, with
5,864–7,297 units at the end:

- `h-stress`: the seat matched the referee on 60/60 checkpoints.
- `2h-stress`: the seats matched on 600/600 shared checkpoints, and each
  matched the referee on 60/60.
- `2h-orders-stress`: the seats matched on 600/600 shared checkpoints, and
  each matched the referee on 60/60. This is an order-autoplay case: live
  orders are bucketed by arrival tick, so the outcome differs between runs.
- `w-allai-stress`: the spectator reached 18,000 ticks (no hash comparison).

During the reruns, each referee was CPU-bound at about 100% of one core, while
the clients used about 63–77%. These games were limited by the referee's speed
and were not stalled. In Retail, the same cases finished inside the default
limit: `h-stress` and `2h-stress` concluded at ticks 5,634 and 6,611, and
`2h-orders-stress` reached the 18,000-tick limit with 6,760 units. This shows
consensus and completion. It does not claim that Legion keeps 4× speed in
dense battles.

**Discarded earlier attempt.** An earlier attempt at this sweep, against
`9b9269b2`, was stopped part-way when more fixes landed on `main`. Its
binaries, worktree and remote files were removed. Everything in this section
was rebuilt and rerun from scratch at `b1e42380`. The harness `TMPDIR` was a
short symlink (`~/r80` → `~/tak-tmp/rr80`) from the start, so the ssh
ControlPath socket stayed under the 108-byte Unix socket limit that stopped
the first 0.7.28 Retail launch.

**Impaired networks.** All eight impaired scenarios ran under netem in both
modes: 50, 100 and 500 ms delay, 100 ms ± 30 ms jitter, 1% and 3% loss, live
orders, and two seated players over 100 ms. For every case, the harness
archived the `__SETUP_OK__` marker, the `prio 1:` root with `netem 30:` on band
1:3, and the u32 filter matching that case's exact game source port (Retail
7927–7934, Legion 8027–8034). Each teardown log ends with only the interface's
default qdisc (`fq_codel` on tak `ens18`, `noqueue` on vpn3 `wg0`). No case
recorded an unclean teardown.

**Isolation.** The referee was a separate test executable,
`~/takserver.sweep080`, using test ports 7890–7936 and 7990–8036 (the public
service uses 7677). It used per-mode map caches
`~/tak_cache.sweep080-{retail,legion}` and per-run replay directories
`~/tak_replay.sweep080-*`. The public `/usr/bin/takserver` on tak.pgnet.us
(PID 9199, port 7677) was not replaced, stopped or restarted. No certificates
were touched. Local clients were pinned to CPUs 4–23.

**Cleanup.** After all runs, both hosts were checked:

- No process was executing `takserver.sweep080`.
- No listener remained on the test ports.
- No `netem` or `prio` qdisc remained on any interface.
- No `/tmp/tak-netem-owner.*` claim file remained. One 1800-second watchdog
  from a Legion impaired case (`net-loss3-orders`) was still sleeping on tak.
  Its claim file was already gone, so it would have exited without running
  `tc`. It was stopped by exact PID.
- The test binary, the sweep's map caches and replay directories, and the
  `/tmp/tak-{srv,val,neg}-*.log` files were removed from both hosts after the
  referee replays had been copied locally.

Locally, no sweep `takclient` or harness process remained. The public
tak.pgnet.us service was still `active`, with the same PID as before the sweep.

**Binaries.** All were built from `b1e42380` (`origin/main`), with build id
`v0.7.28-49-gb1e42380d706` and a clean tree.

- Referee: Ubuntu 24.04 container, GCC 13.3.0, optimized Debug (`-O2 -g`), max
  symbol version GLIBC_2.38. SHA-256
  `5dc470b8a019a90dc0733c04e23e925d1e9557b95de3deba5679fdf508b73e18`
  (identical on both hosts, checked after deployment).
- Client: Fedora, GCC 16.2.1, optimized Debug (`-O2 -g`). SHA-256
  `3ce6a72f8a27cf44d7aa69a41d626e1166fcdd1b155e90daefc0e62620852c45`.
- `override_transfer_test`: SHA-256
  `7ba266156bb56e56aae2380539edeabac2b6cc48708fe4ab16c916adf91ec8d5`.

**Harness.** These runs used the committed `tools/desync-hunt-remote.sh`,
unmodified, from a clean detached worktree. SHA-256
`02d27068863de212b488e19ba8dbe14e9a386b74039dd709defabaabbd6db974`
(byte-identical to the file used for 0.7.25–0.7.28).

## CI and packages

The final source (`477c9238`) passed the Determinism, Linux, Windows and macOS
workflows on `main` (runs 38357355011, 38357355033, 38357355005 and
38357355061). The exact tag passed all four workflows:

- [Determinism and core tests](https://github.com/pocketgeek/tak-engine/actions/runs/38358663588), started manually on the tag because the workflow is path-filtered.
- [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/38358655792): all native package jobs and release attachment.
- [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/38358655827): x64, ARM64 and the x64 signing/package job.
- [macOS](https://github.com/pocketgeek/tak-engine/actions/runs/38358655776): x64 and ARM64 builds, then the new **Sign + notarize** job for each architecture. Apple accepted every submission (release ZIP, debug ZIP and DMG per architecture, each in about a minute), and the job stapled and validated both apps and the DMG and replaced the unsigned release files in the draft.

The release was created as a draft with these notes and tagged CI attached the
packages. All **29 assets** (the same set as 0.7.28) were downloaded and
matched GitHub's SHA-256 digests and byte sizes before publication. Every ZIP
passed its CRC test.

- **Build ids.** The client and server in every Linux package (Debian 12 and
  13, Ubuntu 22.04, 24.04 and 26.04, Fedora 44, openSUSE Leap 16.0, Arch,
  Slackware, both architectures where shipped, and both Linux debug archives)
  carry `v0.8.0` with no `-dirty` suffix; the Debian 13 and Fedora 44 x86-64
  packages and the Linux x64 debug archive report `0.8.0 (build v0.8.0)` when
  run. The Windows client and server in all four ZIPs carry `v0.8.0`, and the
  signed x64 `takserver.exe` reports `0.8.0 (build v0.8.0)` on Windows 11. On
  the arm64 MacBook (macOS 26.6.2) the DMG's `takclient`, the ZIP's
  `takserver` and the debug ZIP's `takclient` report `0.8.0 (build v0.8.0)`.
- **Windows signatures.** Windows CI signed **17** files (16 executables and
  the installer) and verified each signature and timestamp with 0 errors and 0
  warnings. Locally, every executable in the four ZIPs (x86-64 or ARM64 PE as
  named) and the installer carries a certificate table, and on Windows
  `Get-AuthenticodeSignature` reports **Valid** with a timestamp for the
  installer, `takclient.exe` and `takserver.exe`.
- **macOS signing and notarization**, checked on the MacBook against the
  downloaded release files (not the CI artifacts):
  - Both DMGs (arm64 and x64): `xcrun stapler validate` succeeds and `spctl
    --assess --type open --context context:primary-signature` reports
    *accepted, source=Notarized Developer ID*, signed by Developer ID
    Application: Curtis Edge (L479Y59RN8).
  - Both apps inside each DMG and each release ZIP (**Total Annihilation -
    Kingdoms.app** and **TAK Cartographer.app**): stapled ticket validates,
    `spctl --assess --type execute` reports *accepted, source=Notarized
    Developer ID*, `codesign --verify --deep --strict` passes, the hardened
    runtime flag is set, bundle version 0.8.0, and the executables are arm64 or
    x86_64 as named.
  - The bare programs in the release and debug ZIPs carry the Developer ID
    signature with a secure timestamp and pass `codesign --verify --strict`.
    As the notes say, they cannot carry a stapled ticket, so Gatekeeper checks
    them online (`spctl` reports a bare program as valid code that is not an
    app).

Published 2026-10-13 07:07 UTC.

## Screenshots

Done on 2026-10-12 with the 0.8.0 release-preparation build (`takclient
--version` 0.8.0; Debug with `-O2`, OpenGL) and a capture-only patch that is
not committed. Each new or re-rendered image was inspected by eye against its
caption.

- **Gallery.** Re-rendered: `title.jpg` (shows VERSION 0.8.0); `hud.jpg`, now a
  mixed 45-unit formation selection showing the information bar's portrait
  strip, FORMATION 1, 45 SELECTED and solid green `1F` markers (checked
  after 8deab7c6: a re-render's information bar is pixel-identical, since its
  eight types already fit at full size); `flyers.jpg`
  (Legion flyers over the front of the formation, protocol 254); and
  `cartographer.png` (the menu bar's new Symmetry menu). The other gallery
  images are unchanged; none shows the Options screen, a selection, a squad
  marker or the Cartographer.
- **User guide.** New: `guide/selection-strip.jpg`, the information bar of a
  hurt five-type group (GROUP 3, 24 SELECTED) at UI Scale 150%, re-rendered
  after 8deab7c6 so the portraits are full size out over the end-cap scroll, in
  [Selection](user-guide.md#selection); and `guide/cartographer-terrain.jpg`,
  the grouped terrain browser, in [Basics](user-guide.md#basics).
  Re-rendered: `guide/hud-annotated.jpg` and the five `guide/side-*.jpg` (status
  text now centred between the bar pairs), `guide/shift-queue.jpg`,
  `guide/flyers-retail-legion.jpg` (Legion flyers now over the leading ground
  units) and `guide/options-graphics.jpg` (Texture Filtering slider in place of
  Bilinear Filtering).
- **Capture fix.** The capture patch's spawned Legion scene now turns on the
  path service, as match setup does; without it the spawned Legion formation
  had no flyer stations.
- **Labels.** Development scenes and the selection still life are identified in
  their captions and in [the capture notes](img/README.md).
- **Size.** Total tracked image bytes rose from 15.78 MB to 15.96 MB (+0.18 MB,
  mostly the two new pictures).
- **Links.** All relative links, anchors and image paths in the changed
  Markdown resolve.
- **Not captured.** Texture Filtering at Units or Everything (every capture
  profile uses the Off default), the fallback stone information bar, the
  **+K** overflow tile, the Cartographer's symmetry axis overlay, and the macOS
  first launch.

## Limits

Local visual inspection covers Linux OpenGL screenshots only; the Windows and
macOS packages are checked from CI and by the signature, notarization and
build-id checks above, and were not played by hand for this release. The macOS
first launch from a quarantined browser download was not exercised
interactively (the checks above use `spctl`, which applies the same Gatekeeper
assessment). Remote simulated-time sweeps do not substitute for a multi-hour
wall-time soak, and dense Legion stress battles still run below the harness's
speed on the Debug test referees (see above). The factory fix leaves out
retail's release of finished units (rally points or the PARK ring), and an
ordinary flyer move's detached coast still settles before standby (see Scope).
Texture Filtering at Units or Everything, the **+K** overflow tile and the
Cartographer's symmetry axis overlay have no screenshots. The Debug (`-O0`)
builds skip the nightly-labelled tests, as the nightly job only runs them
optimized; under unoptimized Clang `movement_orders_trolls` keeps a thin
margin (278.5 s of 300 s).
