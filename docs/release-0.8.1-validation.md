# 0.8.1 release validation

Protocol **256**, replay format **12**, generator version **8**, campaign
payload **4**, database schema **9** (all unchanged since 0.8.0).

Tagged `v0.8.1` at `24ae9f44` and published on 2026-10-13.

## Scope and compatibility

0.8.1 follows 0.8.0 (protocol 256) and carries no protocol change. After the
0.8.0 validation commit `04a122eb`, the 17 commits `adcb8fdb`..`e5dc92cf` change
only `src/client/`, the client's tests, CMake and documentation:

- **Information bar declutter** (`adcb8fdb`, `12e19f16`). Each element keeps its
  `.gui` spot but draws only with data: the portrait and left HP bar with a
  selection, the left mana bar for a pool, the right name and progress bar only
  with a conjure target; the right mana bar is removed. The first portrait of a
  multi-selection is one slot of the type strip (`selportraits::layout` counts its
  spot in the strip's span). `selportraits_test` checks slot 0 matches its
  neighbours in every layout state. Documented as a deliberate departure from
  retail in `docs/retail-engine.md`.
- **GPU memory** (`83059faa`, `72e68723`, `9360e477`, `e271ba6c`, `8d0416ec`,
  `58a8b026`, `cd817279`, `d9920b85`, `3848cbce`, `244e0776`, `a994f1d1`).
  `gpuvram` post-failure tightening now relaxes after 60 s without a failure,
  doubling per re-tightening up to 8 minutes (`gpuvrampolicy.h`, driven by
  `gpuvram_test` with a fake clock); a settable limit with the Options GPU MEMORY
  LIMIT choice (Auto = 1280 MiB, 384 MiB floor), persisted as `gpuMemoryLimit`;
  terrain fallback sections uploaded on demand with a 64 MiB idle budget and 3 s
  expiry instead of all at load; cached chunks at another mip level stand in while
  the wanted level bakes; under the cap a needed section sheds off-screen chunks;
  colour atlases are never refused (the terrain caches shed first); lightning,
  the Crusades fallback font and campaign map, and the stream capture PBOs and
  renderbuffer are counted. `terrain_cache_test`, `settings_test` and
  `override_settings_test` cover these. Measured peaks are in the
  [release notes](release-0.8.1-notes.md).
- **Options dropdowns** (`910ee228`, `8d0416ec`): open on the side with more room,
  whole rows only, scroll thumb when too long; label column widens to fit.
- **Streaming Remember Key** (`43631028`, `e5dc92cf`): `src/client/credstore.h`
  with one backend per platform (Windows Credential Manager via advapi32, macOS
  Keychain via the Security framework, Linux Secret Service through libsecret
  loaded with `dlopen`, none elsewhere), all store calls on one worker thread
  (`StreamKeyMemory`); only `streamRememberKey` is a setting. `stream_key_test`
  (fake store) and `credstore_test` (real store under a test-only name, exit 77
  when there is none) are new.

**Compatibility.** `kNetVersion` is 256 in `src/net/protocol.h` at `e5dc92cf`,
as at `v0.8.0`. `git diff v0.8.0..e5dc92cf` touches nothing under `src/sim/`,
`src/net/`, `src/server/`, `src/ai/`, `src/campaign/`, `src/tnt/` or the Crusades
store, so replay format 12 (`kReplayFormat`), generator version 8, campaign
payload 4 (`src/net/crusades.h`) and schema 9 (`kSchemaVersion`) are unchanged
and no hashed simulation state can have moved. The server's Hello check
(`Server::handshake`) rejects only a different `kNetVersion` or a different
pure-retail gameplay-data hash; the client sends an empty build id and the server
does not compare engine versions. So **0.8.0 and 0.8.1 clients and servers can
play together in either direction**, and replay playback, which requires
`protocol == kNetVersion` (`supportedReplayProtocol`), accepts 0.8.0 recordings
in 0.8.1 and the reverse. The replay header's `engineVersion` string is
informational. A 0.8.0 build reading a 0.8.1 settings file skips the two new keys
(unknown keys are ignored on read) and drops them on its next save.

`kNetVersion` must be re-checked immediately before tagging (in 0.8.0 the
protocol moved during preparation).

## Local preparation checks

All checks ran on a clean detached worktree of `origin/main` at
`e5dc92cfd49bc8438ecdd3b479177e1ed183ebbe` (protocol **256**, replay format 12).
The version bump lives on the docs branch, so these binaries still call themselves
0.8.0. The owned retail install served as `--data` and `TAK_TEST_DATA`. Toolchains
were GCC 16.2.1, Clang 22.1.8, CMake 4.3.0, Ninja 1.13.2 and Python 3.14.8. Cores
0–3 stay reserved for timing work, so builds and tests ran on cores 4–23.

**Result: every check passed.** There was no crash, desync, sanitizer finding or
stale baseline, so no `release-local-081-baselines` branch was needed, and no fix
was required before tagging. No commit since `v0.8.0` touches `src/sim`, `src/net`,
`src/server`, `src/ai` or `tools/scenarios`. The 18 commits change the client only
(info bar, GPU memory, Options dropdowns, streaming Remember Key) plus docs and
tests. Details follow.

- **Multiplayer compatibility with 0.8.0.** `kNetVersion` is still **256**. The
  server's Hello gate (`src/server/server.cpp`) rejects a client only on a protocol
  mismatch or a gameplay-data hash mismatch. The client's build id is logged but
  not enforced. No gameplay data changed, and the sim, net, server and AI sources
  are byte-identical to `v0.8.0`. So 0.8.1 and 0.8.0 clients and servers can play
  together. Replays are compatible as well: `src/net/replayhdr.h` accepts a replay
  whenever its protocol equals `kNetVersion`, so 0.8.0 replays play in 0.8.1 and the
  reverse. The unchanged `--mpai` hashes below confirm that the simulation itself
  did not move.
- **Builds.** Release, Debug, optimized Debug (`-O2 -g`), Clang Debug and
  AddressSanitizer all-target builds completed with exit code 0: 597 steps in
  Release and 598 in the Debug-type trees (up from 585/586). These were fresh
  trees, with ccache on the GCC builds. They use the static-deps prefix
  (`third_party/static-deps`) and the static Bink/streaming FFmpeg prefix
  (`third_party/ffmpeg-bink`). Every client and server reports
  `takclient (TAK engine) 0.8.0 (build v0.8.0-18-ge5dc92cfd49b)` and
  `takserver (TAK engine) 0.8.0 (build v0.8.0-18-ge5dc92cfd49b)`. The build identity
  is clean, with no `-dirty` suffix. The server announces `protocol v256`.
- **Full CTest.** There are now 555 registered tests (541 in Release), up from 552.
  160 of them carry the `nightly` label, as in 0.8.0. The 3 new tests are
  `gpuvram` (the relaxing post-failure cap, the settable limit and the policy),
  `credstore` (an OS credential-store round trip) and `stream_key` (the Remember
  Key flow: the key never reaches serialized settings). The existing
  `terrain_cache` and `selportraits` tests were extended for on-demand map sections
  and the scaled first portrait.
  - **Release: 541/541 passed. Optimized Debug: 555/555 passed.** Both ran the full
    suite, nightly label included. The worktree had an `assets` link to the real
    `assets` directory, so `cobanim` ran its 11 checks.
  - **Debug and Clang Debug ran with `-LE nightly`**, as in 0.8.0. At `-O0` some
    nightly tests exceed their timeouts, and the nightly job only runs them at
    `-O2`. **Debug: 395/395 passed. Clang Debug: 395/395 passed.** The two suites
    ran one after the other.
  - **`credstore` ran for real.** It round-tripped through the session's Secret
    Service (KWallet) under the test-only service name "TAK Engine Test" and
    removed its entry. It skips when no store is available. No real stream key
    was involved.
  - **All 67 `legion_check_*` scenario gates passed** in Release and optimized
    Debug against the unchanged `tools/scenarios/baseline.json`.
    `legion_check.py validate --baseline tools/scenarios/baseline.json` reports
    0 problems.
  - **Battle cost and naval tests passed.** `legion_cost`,
    `legion_scenario_battle-field-2x500`, `legion_scenario_battle-assault`,
    `naval_ai` and `legion_acceptance_crowdheld_legion` passed in Release and
    optimized Debug, with no ratchet failure.
  - **Slowest tests.** In Release and optimized Debug: `legion_check_pocket-615-open`
    (841.3 s / 870.1 s), `legion_check_pocket-615-wall` (785.3 s / 868.6 s) and
    `legion_check_tail-corner380` (780.8 s / 841.2 s). All are under their 1800 s
    limit. In Debug and Clang Debug the slowest was `legion_w7_awarebig`
    (302.5 s / 322.8 s).
  - **Headroom note.** Under unoptimized Clang, `movement_orders_trolls` took
    263.1 s against its 300 s limit (278.5 s in 0.8.0). It passed, but the margin is
    still thin.
- **Focused AddressSanitizer/leak run: 144/144 passed.** The selection is the 141
  tests from 0.8.0 plus the new `gpuvram`, `credstore` and `stream_key`. It already
  included `terrain_cache` and `selportraits`. The run used
  `bash tools/run-ctest-selection.sh` with `SDL_SHUTDOWN_DBUS_ON_QUIT=1`, dummy SDL
  video/audio, `ASAN_OPTIONS=detect_leaks=1` and no suppressions. There were no
  AddressSanitizer, LeakSanitizer or runtime-error reports. That includes the
  runtime-loaded libsecret path in `credstore`.
- **Retail group emulation check (formation-air).** I ran
  `python3 -B tools/re/check_retail_group.py` (400 cases by default) against the
  emulated `KINGDOMS.icd`. It passed every section: tick 400, query 4000, check
  2000, formation 2000, vtolcheck 2000 and vtolform 2000. Every outcome count is
  identical to 0.8.0.
- **Determinism.** In `tools/check-determinism.sh`, the GCC and Clang x86-64 builds
  at O0/O2/O3 all agree on `dcef618cd2e4d558`, the same as 0.8.0. The aarch64 legs
  are skipped because target headers are missing; native ARM64 CI remains a separate
  gate. `tools/check-detmath.sh` passes, including the `std::atomic_ref` guard.
- **`--mpai` golden hashes.** All four runs were made twice and produced the same
  hash both times. The client and server logs contain no desync or referee-suspect
  line. The runs used the optimized Debug build, Inner Circle,
  `takserver --local --no-auth --seed 1`, an empty `XDG_DATA_HOME`, and
  `TAK_LEGION=1` for the Legion runs. **Every hash is unchanged from 0.8.0**, as
  expected, since no sim change landed.

  | Mode | Duration | Hash (both runs) | 0.8.0 |
  | --- | ---: | --- | --- |
  | Legion | 300 s | `21055e66adec40d6` | `21055e66adec40d6` |
  | Retail | 300 s | `95b05c8290b55317` | `95b05c8290b55317` |
  | Legion | 60 s | `025e4a35abfcd04e` | `025e4a35abfcd04e` |
  | Retail | 60 s | `84fdef37bbb3606e` | `84fdef37bbb3606e` |

  Optimized Debug binary SHA-256:
  - client `4b376efdad489bbbe7563a7efda0e1ca6c8051b4fd22b443e8f72bd41d7232f4`
  - server `72f2ef04672456f5e197d5b8caa11cc4a6be681ef876fb2680d48582ff224524`
- **Python research suite.** **165 tests** passed, with one skip: the optional corpus
  test, which runs only when `TAK_AUTH_RETAIL_DIR` is set. The suite ran after the
  Release build, so the `build/hpitool` tests ran too. The separate owned-retail
  archive verifier (`TAK_AUTH_RETAIL_DIR`) passed all **6 tests**, including the
  corpus and in-memory negative checks. I took SHA-256 sums of the 16 retail HPI
  archives and `Kingdoms.key` before and after the runs. They are unchanged, and
  identical to the 0.8.0 run.
- **Syntax checks.** All **438** tracked Python files compile. All **21** tracked
  `*.sh` scripts and the extensionless `tools/tak-netem` pass `bash -n`.
- **Notes (informational).**
  - The Release build emits 530 warnings. The set is identical to 0.8.0, file by
    file, including the one vendored-SQLite `-Wstringop-overread` false positive.
    The new credential-store, stream-key and GPU-memory code adds no warning.
  - `tools/run-ctest-selection.sh` is still tracked as mode 100644, so it must be
    run with `bash`.
  - A worktree outside the main checkout still needs an `assets` link for
    `cobanim`.

Logs: `~/tak-tmp/scratch/release-081/local/` (`cfg-*.log`, `make-*.log`,
`build-status.txt`, `versions.txt`, `toolchains.txt`, `tests-o2.txt`,
`ctest-*.log`, `ctest-status.txt`, `ctest-asan.log`, `asan-list.txt`, `mpai-*.log`,
`mpai-results.txt`, `o2-sha256.txt`, `check-determinism.log`, `check-detmath.log`,
`check_retail_group.log`, `legion-validate.log`, `python-suite.log`,
`verifier.log`, `pycompile.log`, `bashn.log`, `retail-sha-{before,after}.txt`).

## Remote multiplayer sweep

The full 37-scenario table passed for both shipped pathfinding modes, Retail and
Legion, on **2026-10-13 (17:11–17:55 UTC)**. Referees ran on `tak.pgnet.us` and
`vpn3.pgnet.us`, with optimized local clients. Each scenario allowed ten
simulated minutes (18,000 ticks) or ended at an actual result for that seat.
Real seated players provide the client/referee checksum coverage. The two
all-AI spectator scenarios per mode test flow control, not hashes.

The harness picks the mode through the hosting client's room options:
`TAK_LEGION=1` creates a Legion room, and without it the room is Retail. Every
recorded client and referee replay header (68 Retail, 76 Legion) confirms the
intended mode, replay format 12 and protocol 256. The headers carry engine
version 0.8.0 because the sweep ran on `origin/main` before the 0.8.1 version
bump, which changes only the version string.

| Mode | Completed scenarios | Completed seats | Client-pair checkpoints | Client–referee checkpoints |
| --- | ---: | ---: | ---: | ---: |
| Retail | 37/37 | 52/52 | 11,205 across 22 seat pairs, 0 mismatched | 1,063 across 20 seat/referee comparisons, 0 mismatched |
| Legion | 37/37 (single pass, no retries) | 52/52 | 11,607 across 22 seat pairs, 0 mismatched | 1,546 across 27 seat/referee comparisons, 0 mismatched |

Each mode passed both negative gates before its table started. The referee
reported the planted desync (`client 1 DESYNCED at tick 900`), and the
override-transfer test rejected the incorrect loaded gameplay data
(`verified packs transferred and incorrect loaded gameplay rejected`). The
build-identity gate also passed in each mode (`both at v0.8.0-18-ge5dc92cfd49b`).
The gate checks only `tak.pgnet.us`, so the referee on `vpn3.pgnet.us` was
checked by hand after deployment: the same SHA-256 and the same `--version`
build id. No natural `DESYNCED`, `REFEREE SUSPECT` or client-reported desync
appeared in any case log. The only `err=desync detected` lines are the planted
ones in the validation client logs.

The checkpoint counts compare the per-tick hash records in the seats'
`.takrep` files (every 30 ticks). The client-referee column compares the seats
with referee replays (every 300 ticks). Each format-12 checkpoint carries a
position digest as well as the state hash, and the comparison script checks
both wherever both sides recorded one. All 2,385 referee checkpoints and all
56,515 seated checkpoints carried a digest. Before the counts were trusted, the
script was checked against a copy of one game's replay (`2h-baseline`, Retail)
with one hash bit flipped, and then one digest bit flipped. It reported a
mismatch both times. Referee replays were matched to their cases by the seed in
the replay header (seed = 2000 + scenario index), not by port. The port was
also checked, and it agreed for every replay. The referee counts are partial:
the harness stops each referee as soon as its clients exit, so only 17 Retail
games (15 seated) and 25 Legion games (23 seated) wrote a referee replay. The
referee's live check runs on every seated game, whether or not a replay was
written.

**No Legion retries.** In 0.7.25–0.8.0, the dense Ulasem Arena stress cases ran
past the default 900-second real-time limit in the first Legion pass, and were
rerun with `--only`. This time the Legion pass ran with `TAK_WALL_TIMEOUT=3600`
from the start, with the same seeds and the same simulated-time limit. It
changes only the real-time deadline, not the tick or hash checks. Every Legion
seat completed in the single pass. `h-stress`, `2h-stress` and
`2h-orders-stress` all reached 18,000 ticks with 5,864–7,297 units. `h-stress`
finished about 19 minutes after the pass started, so the default limit would
have cut it again. The Retail pass used the default limit, and every case
finished inside it: `h-stress` and `2h-stress` concluded at ticks 5,606 and
6,611, and `2h-orders-stress` reached 18,000 ticks with 6,669 units.

**Reproducibility across releases.** `src/sim`, `src/ai`, `src/net` and
`src/server` are unchanged since the 0.8.0 sweep commit (`b1e42380`) and since
the `v0.8.0` tag. In Retail, 30 of the 37 cases ended with the same tick and
hash as the 0.8.0 sweep, and in Legion 31 of 37. Every differing case but one
is an order-autoplay case. Those cases bucket live orders by arrival tick, so
their hashes are not expected to repeat. The exception is Retail `h-stress`
(seed 2003). In 0.8.0 its seat concluded at tick 5,634 (`c8293ed0b1e62c64`).
This sweep concluded at tick 5,606 (`c93ae6becfcde6ca`), the same tick and hash
as the 0.7.28 sweep. That is not a desync, because in every run the seat and
the referee agreed. It was checked further:

- The 0.8.0 and 0.8.1 seat replays match at all 187 shared 30-tick checkpoints,
  through tick 5,580. They differ only in when the result arrived.
- Two more runs of the case on the same binaries, both behind all three gates,
  both ended at tick 5,606 with hash `c93ae6becfcde6ca`. All three 0.8.1 seat
  replays are identical, and each matched its referee at 19 of 19 checkpoints.

The 0.8.0 result therefore looks like a rare run-to-run variation late in this
dense game, after tick 5,580. It exists in the unchanged sim and is not a 0.8.1
regression. It is noted for a follow-up investigation. It does not block the
release.

**Discarded first launch.** The first Retail launch omitted `--validate`, so the
three gates did not run. It was stopped with SIGTERM before any result was read,
and the harness cleanup ran. Its remote replays were deleted, and the pass was
rerun from the start with the gates.

**Impaired networks.** All eight impaired scenarios ran under netem in both
modes: 50, 100 and 500 ms delay, 100 ms ± 30 ms jitter, 1% and 3% loss, live
orders, and two seated players over 100 ms. For every case, the harness
archived the `__SETUP_OK__` marker, the `prio 1:` root with `netem 30:` on band
1:3, and the u32 filter matching that case's exact game source port (Retail
7927–7934, Legion 8027–8034). Each teardown log ends with only the interface's
default qdisc (`fq_codel` on tak `ens18`, `noqueue` on vpn3 `wg0`). No case
recorded an unclean teardown.

**Isolation.** The referee was a separate test executable,
`~/takserver.sweep081`, using test ports 7890–7936 and 7990–8036 (the public
service uses 7677). It used per-mode map caches
`~/tak_cache.sweep081-{retail,legion}` and per-run replay directories
`~/tak_replay.sweep081-*`. The public `/usr/bin/takserver` on tak.pgnet.us
(PID 20047, port 7677, active since 07:32:44 UTC) was not replaced, stopped or
restarted. No certificates were touched. Local clients were pinned to CPUs
4–23.

**Cleanup.** After all runs, both hosts were checked:

- No process was executing `takserver.sweep081`.
- No listener remained on the test ports.
- No `netem` or `prio` qdisc remained on any interface.
- No `/tmp/tak-netem-owner.*` claim file remained. Four 1800-second watchdogs
  from Legion impaired cases were still sleeping: three on tak
  (`net-loss3-orders`, `net-2h-rtt100`, `net-rtt500`) and one on vpn3
  (`net-orders100`). Their claim files were already gone, so they would have
  exited without running `tc`. They were stopped by exact PID.
- The test binary, the sweep's map caches and replay directories, and the
  `/tmp/tak-{srv,val,neg}-*.log` files were removed from both hosts after the
  referee replays had been copied locally.

Locally, no sweep `takclient` or harness process remained. The public
tak.pgnet.us service was still `active`, with the same PID as before the sweep.

**Binaries.** All were built from `e5dc92cf` (`origin/main`), with build id
`v0.8.0-18-ge5dc92cfd49b` and a clean tree. Protocol `kNetVersion` was 256.

- Referee: Ubuntu 24.04 container, GCC 13.3.0, optimized Debug (`-O2 -g`), max
  symbol version GLIBC_2.38. SHA-256
  `34ece1ae18e24af4c16fc6182043dbf8a7c1e1c9684fca9b4b15e1b793d11c9f`
  (identical on both hosts, checked after deployment).
- Client: Fedora, GCC 16.2.1, optimized Debug (`-O2 -g`). SHA-256
  `4b376efdad489bbbe7563a7efda0e1ca6c8051b4fd22b443e8f72bd41d7232f4`.
- `override_transfer_test`: SHA-256
  `7ba266156bb56e56aae2380539edeabac2b6cc48708fe4ab16c916adf91ec8d5`
  (byte-identical to the 0.8.0 build).

**Harness.** These runs used the committed `tools/desync-hunt-remote.sh`,
unmodified, from a clean detached worktree. SHA-256
`02d27068863de212b488e19ba8dbe14e9a386b74039dd709defabaabbd6db974`
(byte-identical to the file used for 0.7.25–0.8.0).

**Mixed versions.** No game mixing a 0.8.0 build with a 0.8.1 build was played. The compatibility statement in Scope rests on the source (the unchanged protocol, the server's Hello gate and the replay loader) and on the unchanged `--mpai` hashes and sweep results above.

## CI and packages

The final source (`24ae9f44`) passed the Determinism, Linux, Windows and macOS
workflows on `main` (runs 38445645281, 38445645302, 38445645509 and
38445645342). The exact tag passed all four workflows on the first attempt,
with no reruns:

- [Determinism and core tests](https://github.com/pocketgeek/tak-engine/actions/runs/38448261290), started manually on the tag because the workflow is path-filtered.
- [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/38448255161): all native package jobs and release attachment.
- [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/38448254852): x64, ARM64 and the x64 signing/package job.
- [macOS](https://github.com/pocketgeek/tak-engine/actions/runs/38448255176): x64 and ARM64 builds, then the **Sign + notarize** job for each architecture, which replaced the unsigned release ZIPs and DMGs in the draft with the signed, notarized and stapled ones (the release DMGs are larger than the CI-artifact DMGs by the stapled ticket).

The release was created as a draft with these notes and tagged CI attached the
packages. All **29 assets** (the same set as 0.8.0) were downloaded and
matched GitHub's SHA-256 digests and byte sizes before publication. Every ZIP
passed its CRC test.

- **Build ids.** The client and server in every Linux package (Debian 12 and
  13, Ubuntu 22.04, 24.04 and 26.04, Fedora 44, openSUSE Leap 16.0, Arch,
  Slackware, both architectures where shipped, and both Linux debug archives),
  in all four Windows ZIPs, and in both macOS DMGs, release ZIPs (bare programs
  and inside the app) and debug ZIPs carry `v0.8.1` with no `-dirty` suffix.
  Run: the Debian 13 and Fedora 44 x86-64 packages and the Linux x64 debug
  archive report `0.8.1 (build v0.8.1)` for both programs; the signed x64
  `takserver.exe` reports `0.8.1 (build v0.8.1)` on Windows 11; on the arm64
  MacBook (macOS 26.6.2) the DMG app's `takclient`, the release ZIP's
  `takserver` and the debug ZIP's `takclient` report `0.8.1 (build v0.8.1)`.
- **No new runtime dependency.** No packaged Linux `takclient` (all 18) has a
  `NEEDED` entry for libsecret, GLib, GIO or GObject, and no Debian, RPM or
  Arch package declares one; libsecret is loaded at run time only. The Windows
  x64 `takclient.exe` imports exactly the same DLL set as 0.8.0 (`advapi32.dll`
  was already imported for TLS).
- **Windows signatures.** Every executable in the four Windows ZIPs (16, x86-64
  or ARM64 PE as named) and the installer carries a certificate table, and on
  Windows `Get-AuthenticodeSignature` reports **Valid** with a timestamp for
  the installer, `takclient.exe` and `takserver.exe` (signer Curtis Edge).
- **macOS signing and notarization**, checked on the MacBook against the
  downloaded release files (not the CI artifacts):
  - Both DMGs (arm64 and x64): `xcrun stapler validate` succeeds and `spctl
    --assess --type open --context context:primary-signature` reports
    *accepted, source=Notarized Developer ID*, signed by Developer ID
    Application: Curtis Edge (L479Y59RN8).
  - Both apps inside each DMG and each release ZIP (**Total Annihilation -
    Kingdoms.app** and **TAK Cartographer.app**): the stapled ticket
    validates, `spctl --assess --type execute` reports *accepted,
    source=Notarized Developer ID* and `codesign --verify --deep --strict`
    passes.
  - The bare programs in the release ZIPs and the debug ZIP's `takclient` pass
    `codesign --verify --strict`; as in 0.8.0 they cannot carry a stapled
    ticket, so Gatekeeper checks them online.

Published 2026-10-13 18:58 UTC.

## Screenshots

Done on 2026-10-13 with the 0.8.1 release-preparation build (`takclient
--version` 0.8.1, build `v0.8.0-18-ge5dc92cfd49b-dirty` from the version bump and
the capture patch; Debug with `-O2`, OpenGL) and a capture-only patch that is not
committed. Each re-rendered image was inspected by eye against its caption.

- **Why so many.** The information bar change made every picture that shows the
  bar stale: with nothing selected the old pictures showed an empty black
  portrait box and empty bar outlines, and with a selection an empty right-hand
  bar. `12e19f16` had fixed `guide/hud-annotated.jpg` by covering the gauges with
  neighbouring bar art and `guide/selection-strip.jpg` by cropping; both are now
  real re-renders.
- **Gallery.** Re-rendered: `title.jpg` (shows VERSION 0.8.1); `hud.jpg` (the
  45-unit formation selection, no right-hand bar; its stats panel's VID row reads
  87 MB, against 151 MB in the 0.8.0 capture); `flyers.jpg` and `naval.jpg` (empty
  information bar); `diplomacy.jpg` (Elsin's bar without the right-hand bars; the
  fixture now renders fog of war, which the 0.7.25 picture did not show). The
  capture patch's larger naval fleet, dropped after 0.7.26, was added back.
  `streaming.jpg` already shows **REMEMBER KEY** (rendered for `43631028`) and was
  kept. `battle.jpg`, `dragons.jpg` and `generated.jpg` are spectator views without
  the bar; the menu pictures and the Cartographer pictures are unaffected.
- **User guide.** Re-rendered: `guide/hud-annotated.jpg` (same eight markers),
  `guide/selection-strip.jpg` (same 1160 × 110 crop), the five `guide/side-*.jpg`,
  `guide/shift-queue.jpg`, `guide/flyers-retail-legion.jpg`,
  `guide/fire-on-the-move.jpg` (now the 0.8.0 portrait strip, 2 Mage Builders and
  9 Archers, 11 SELECTED, instead of the 0.7.28 single portrait),
  `guide/campaign-briefing.jpg`, `guide/replay.jpg` (a fresh recording of the same
  scripted Legion move, played back by the same build) and
  `guide/options-graphics.jpg` (scrolled one step further so the whole Graphics
  section shows, ending with **GPU MEMORY LIMIT** at AUTO (1.25 GB)).
  `guide/benchmark.jpg`, `guide/scorecard.jpg` and `guide/tactical-dots.jpg` are
  spectator views and were kept; `guide/cartographer-terrain.jpg` is unaffected.
- **Labels.** Development scenes and the selection still life are identified in
  their captions and in [the capture notes](img/README.md).
- **Size.** Total tracked image bytes fell from 15.90 MB to 15.77 MB (−0.13 MB).
- **Links.** All relative links, anchors and image paths in the changed
  Markdown resolve.
- **Not captured.** The GPU Memory Limit dropdown open, **LOWERED FOR NOW**,
  Remember Key ticked or greyed out (**NO SYSTEM PASSWORD STORE**), a
  conjure target's right-hand bar, the **+K** overflow tile, and Windows and
  macOS rendering.

## Limits

No game mixing 0.8.0 and 0.8.1 builds was played: the compatibility statement
rests on the source (unchanged protocol, Hello gate and replay loader), the
unchanged `--mpai` hashes and the unchanged sweep results. Local visual
inspection covers Linux OpenGL screenshots only; the Windows and macOS packages
are checked from CI and by the signature, notarization and build-id checks
above, and were not played by hand for this release. Remember Key was
exercised against a real store only on Linux (`credstore` through KWallet);
the Windows Credential Manager and macOS Keychain backends were built and
signed by CI, and the panel logic is covered by `stream_key` against a fake
store, but neither backend was run against a real store for this release. **LOWERED FOR NOW** needs a driver out-of-memory error, which OpenGL
does not report, so the relaxing limit is covered by `gpuvram` with a fake
clock rather than on Direct3D or Metal. The video-memory figures in the notes
come from one Linux OpenGL machine at 3840 x 2160. The macOS first launch
from a quarantined browser download was not exercised interactively (`spctl`
applies the same Gatekeeper assessment). Remote simulated-time sweeps do not
substitute for a multi-hour wall-time soak; the Retail `h-stress` end-tick
difference from the 0.8.0 sweep (see the remote sweep) is left for a
follow-up. The Debug (`-O0`) builds skip the nightly-labelled tests, as the
nightly job only runs them optimized; under unoptimized Clang
`movement_orders_trolls` keeps a thin margin (263.1 s of 300 s).
