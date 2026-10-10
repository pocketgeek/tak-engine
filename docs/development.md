# Build and development guide

[Back to the README](../README.md) · [User guide](user-guide.md)

The engine version is set by `project(... VERSION ...)` in `CMakeLists.txt`.
It is separate from the multiplayer protocol version in `src/net/protocol.h`.

## Building

Use CMake ≥ 3.24, C and C++20 compilers (GCC/Clang or MinGW-w64), Ninja, Git,
Make, and pkg-config. x86 FFmpeg builds also need NASM for SIMD support.
Linux streaming dependencies need Python 3, patch, and Perl standard modules (`perl-core` on Fedora). Linux and macOS builds require the vendored static
zlib, libjpeg-turbo, SDL2, and Bink/streaming FFmpeg libraries. Installing a system
SDL2 package alone is not sufficient.

On Linux, install development headers for the SDL video/audio backends you need
(X11/Wayland, ALSA/PulseAudio, OpenGL/EGL). Backends whose headers are absent
when SDL is built may be unavailable. Linux also requires the static C++ runtime
archives; on Fedora these include `libstdc++-static`. The exact package lists
used by CI are in [linux.yml](../.github/workflows/linux.yml).

From the repository root:

```sh
./tools/build-ffmpeg-bink.sh
./tools/build-static-deps.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

The dependency scripts download their sources and reuse existing installations
under `third_party/`. Their `PREFIX` environment variable sets the destination;
pass matching `-DTAK_FFMPEG_PREFIX=...` and `-DTAK_STATIC_DEPS_PREFIX=...`
CMake options when using custom locations. Missing required libraries fail
configuration rather than silently switching to shared libraries.

The Crusades persistence library builds SQLite 3.53.4 statically from its
[official amalgamation](https://www.sqlite.org/download.html). CMake downloads
the pinned, SHA-256-verified archive on first configure; subsequent builds reuse
the extracted source under the build directory. For offline configuration,
set `-DFETCHCONTENT_SOURCE_DIR_TAK_SQLITE=/path/to/sqlite-amalgamation-3530400`
to an extracted copy containing `sqlite3.c` and `sqlite3.h`. It adds no runtime
SQLite DLL/shared-library dependency. The opt-in authenticated Crusades service
supports tactical battles, territory browsing, matchmaking, results and retained
replays. Operators supply campaign definitions and battle-map assignments.
Historical territory-capture and campaign-victory rules remain incomplete; see
the [operations guide](research/darien-crusades/campaign-operations.md).

For Clang AddressSanitizer builds, select both `CMAKE_C_COMPILER=clang` and
`CMAKE_CXX_COMPILER=clang++`: SQLite is compiled as C, so mixing GCC C with
Clang C++ can introduce incompatible sanitizer runtimes.

For developer launch modes, diagnostics, and headless harnesses, use a Debug
build. An optimized Debug build keeps those features while improving performance:

```sh
cmake -S . -B build-o2 -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS_DEBUG="-O2 -g" -DTAK_TEST_DATA=/path/to/tak_install
cmake --build build-o2 -j4
ctest --test-dir build-o2 --output-on-failure
```

`TAK_TEST_DATA` enables retail-data tests in addition to the data-independent
suite. Some animation checks also require extracted scripts under
`assets/extracted/all/scripts`. Rebuild **all targets** after simulation, AI, or
network changes so the client, server, and tools use the same code.

### Static dependencies and menu videos

Menu door clips use Bink video decoded by the bundled, minimal FFmpeg build.
No separate FFmpeg installation is needed at runtime. FFmpeg is required for a
source build; a missing or unreadable clip can fall back to static menu art.
The dependency script also builds H.264/AAC streaming with static x264 (GPL),
compatible with the engine's GPLv3 license. See [streaming](streaming.md) for
platform backends, dependency boundaries and tests.

SDL2, libjpeg, zlib, and FFmpeg are linked statically. Linux also embeds the
GCC/C++ runtime but retains system C libraries; SDL loads available windowing and
audio backends at runtime. Windows binaries need no separately bundled SDL,
JPEG, zlib, FFmpeg, or MinGW runtime DLLs. macOS uses system libraries and
frameworks. CI checks these dependency boundaries for release packages.

### Platform builds and releases

- **Windows x64:** use the MSYS2 **MINGW64** environment with its GCC, CMake,
  Ninja, SDL2, libjpeg-turbo, zlib, and pkgconf packages, plus Git, Make, and patch.
  Run `./tools/build-ffmpeg-bink.sh`, then configure and build as above.
  CMake uses the toolchain's static dependency archives; the native
  `build-static-deps.sh` step is not needed. See
  [windows.yml](../.github/workflows/windows.yml) for the exact package list and
  installer build.
- **Windows ARM64:** use Windows 11 ARM64 and MSYS2 **CLANGARM64** with its
  native Clang/LLVM and ARM64 dependency packages. Set `CC=clang`, `CXX=clang++`,
  `TARGET_OS=mingw32`, and `TARGET_ARCH=aarch64` before running the FFmpeg script
  and CMake commands. As on Windows x64, use the toolchain's static archives
  instead of `build-static-deps.sh`. The Windows workflow runs natively on
  `windows-11-arm`, checks executable architecture and static dependency imports,
  and produces Release and Debug portable ZIPs. ARM64 ZIPs are published starting with
  version 0.7.19; the NSIS installer remains x64 only. The pinned FFmpeg version
  excludes NVENC on Windows ARM64; x264 CPU streaming remains available. Qualcomm
  hardware encoding is not implemented.
- **macOS ARM64 and Intel x64:** install Xcode Command Line Tools, then
  `brew install cmake ninja pkg-config`. Run both dependency scripts and the
  source-build commands above, adding `-DCMAKE_FIND_FRAMEWORK=LAST` at configure
  time. CI builds and tests each architecture on a native runner, checks that
  executables link only system libraries/frameworks, and packages signed app
  bundles, DMGs, and Release/Debug ZIPs. CI packages target macOS 14 or later;
  use `MACOSX_DEPLOYMENT_TARGET=14.0` for matching source builds. Intel packages
  are published starting with version 0.7.19, as well as CI artifacts.
  See [macos.yml](../.github/workflows/macos.yml) for app/DMG packaging.
- **Linux x64 and ARM64:** native CI runners build Ubuntu 22.04/24.04/26.04,
  Debian 12/13, Fedora 44, and openSUSE Leap 16.0 containers for each architecture. Arch's official
  container builds x64 only. Choose the package matching both your distribution
  and architecture (`amd64`/`arm64` for DEB, `x86_64`/`aarch64` for RPM).
  ARM64 uses GitHub's [native ARM runners](https://docs.github.com/en/actions/reference/runners/github-hosted-runners),
  not emulation. Dependencies and caches are isolated by distro and architecture;
  both architectures run the same package-workflow regression tests, ELF checks,
  package metadata checks, and static-link gate. Ubuntu 22.04 supplies each
  architecture's Debug tarball. ARM64 artifacts are available from CI and will
  be attached to releases starting with 0.7.23. This does not cover 32-bit ARM or certify
  graphical performance on individual ARM boards.
- **Slackware64 15.0:** CI builds inside the Slackware 15.0 container referenced by
  [SlackDocs](https://docs.slackware.com/howtos:misc:slackware_docker_image), using
  its native toolchain and libraries. `packaging/slackware/package.sh` stages
  CMake's install tree and creates a `.txz` with Slackware's `makepkg`; CI installs
  it and checks client/server startup. The package includes no active systemd unit
  because Slackware uses BSD-style init. Launch the server directly or configure
  local startup yourself. No daemon starts during installation.

The platform workflows build Release and selected Debug artifacts on every
`main` push. The determinism workflow runs for relevant source changes; Windows
and macOS also check the math golden hash natively. Retail-data tests run locally
because the game assets are not included in the repository or CI.

To cut a release, update the CMake project version and README version, commit and
push, and wait for platform checks to pass. Then tag that commit and push the tag:

```sh
git tag -a vX.Y.Z -m "TAK Engine X.Y.Z"
git push origin vX.Y.Z
```

Tags matching `v*` trigger package builds and GitHub Release uploads. Check every
platform job and the complete asset set, then update the release notes; workflows
can create/publish the release with generated notes during upload.
To collect packages privately, create a draft release for the pushed tag before
the upload steps run. Upload workflows reuse that draft; publish it only after
the platform checks, Windows signing and complete asset verification pass.

## Faster verification loop

- **Builds.** CMake uses `ccache` and `mold` automatically when they are installed
  (a second build of the same sources, or of another worktree, is mostly cache hits).
- **Targeted tests: `ctest -L quick -j 8`.** Every test that is not `nightly` and takes
  under 5 s is labelled `quick` (about 255 tests, roughly 30 s at `-j 8`, against about
  6 minutes for the full suite). Use it for a task's own checks; run the full suite
  (`ctest -j 8`) before handing a change over. Other labels: `legion`, `w2`, `nightly`
  (`ctest -L legion`, `-LE nightly`).
- **Long tests start first.** `tools/ctest_costs.txt` lists the tests of 5 s or more;
  CMake turns it into `COST` properties, so even in a fresh build dir (no
  `CTestCostData.txt` yet) the 150 s scenario runs start at once instead of last. After adding a
  slow test, or when timings move, regenerate it from a full local run:
  `ctest --test-dir build-o2 -j 8 && tools/ctest_costs.py build-o2`.
  The longest scenario tests (`legion_gen1_route_*`, `legion_situation_*`) are split
  into parallel parts (per start offset and per mode respectively); `legion_scenario`
  checks serial == workers per mode and offset and gates nothing across them, so the
  parts together are exactly the old test.
- **Exact-identity harness: `tools/legion_identity.sh --base <build> --cand <build>`.**
  Base results are cached under `~/.cache/tak-identity/<base commit>/<build type>-v<N>/`
  (every replay, golden, `--mpai` game, ctest run, `check-determinism.sh` and crowdbench
  matrix row), so a chain of steps runs each base once and only the candidate afterwards.
  The cache is used only for a clean base tree whose build dir builds (`cmake --build`
  is run first); only complete, successful base results are stored; the candidate is
  never read from the cache, so a hashed-state change always shows as DIFF; a run that passes
  with every row SAME also stores the candidate's results under the candidate's own commit, so in
  a chain A->B, B->C the second step finds its base warm. `--no-cache` ignores it,
  `-j N --cores a-b` sets the job width and CPU list, `--quick` also leaves the `nightly` tests out of
  its ctest section (they were 90% of a quick run), and crowdbench now starts beside the
  tail of the replays instead of after them. Bump `HARNESS_VERSION` in
  `tools/legion_identity.py` when a base row's definition changes. The 2-hour
  replays are platform independent, so running them on the Windows or macOS test hosts
  is possible but not automated (follow-up): the harness relies on `taskset`/`setsid`.

## WE timing rounds (`tools/we_timing.sh`)

Engine-wide perf (WE) steps are judged by swapped, concurrent A/B pairs on the reserved timing cores 0-3
(`/home/pocket_geek/tak-tmp/tools/timing.sh`, which flocks them; everything else runs on 4-23). Replaying R-2h / L-2h /
R-bench / L-bench in full, twice, swapped, took about 70 minutes a round. `tools/we_timing.sh` does the same job in
about 10 minutes for two swapped rounds:

```sh
tools/we_timing.sh --base <base build-o2> --cand <cand build-o2> [--rounds 2] [--sits R-2h-1800,...] [--counters]
tools/we_timing.sh --base ... --cand ... --replays <dir of .takrep>   # exact mode, see below
```

- **Situations** (`tools/scenarios/we-timing/*.scn.gz`) are worlds cut out of the four WE recordings by
  `tools/we_timing_harvest.sh <takclient> <replay> <tick> <out.scn.gz>` (TAK_SITUATION, `src/client/situation.h`):
  every live body with its exact position, heading, hit points, speed, id and player goals, the recording's commands for
  the next 3000 ticks (`TAK_SITUATION_CMDS`), clock and RNG, the explored masks and the teams. The map is named
  (`map named Ulasem Arena`), so the files need `--data`. `R-2h-1800` and `L-2h-1800` are cut at tick 1800 (11.9k units,
  falling through the 9.5-10.5k window), the bench ones at 1900 (just after the 60 s benchmark ramp: the ramp's spawns are
  World::tick work a situation cannot replay). Re-cut with the o2 `takclient` of the tree whose recordings play back
  (`legion_identity.py patch-replay` re-stamps the protocol) when Legion behaviour changes: an L situation holds the
  trajectory of the build that cut it.
- **Run shape.** Per situation, two swapped concurrent pairs (base on 0-1 + candidate on 2-3, then swapped) per round:
  `legion_scenario --workers --catchup 60 --ticks warm+measure` with `TAK_PHASE=1`. Defaults: warm-up 1000 ticks (dropped),
  measure 1000. `--catchup N` gives the path service an unlimited budget for the first N ticks: a situation re-issues
  7k in-flight orders at tick 0, and the retail request queue would otherwise drain that backlog over ~2000 ticks
  (the long waypoint queues of ~900 units, which findTarget walks, take that long to reappear). The report prints, per
  situation and SIMPHASE phase, the median of each build's per-run mean ms, their ratio, and the median and range of the
  per-pair ratios (the headline: pair partners run concurrently, which cancels most host noise), plus the final state hash
  of base and candidate (a hash-identical step must print `identical`). `--counters` adds the TAK_SIMSTATS work counters
  (do not compare ms from a `--counters` run).
- **Both trees need the tool files** (`tools/legion_scn.h`, `tools/legion_scenario.cpp` `--catchup`, `src/client/situation.h`,
  `src/client/ordershape.h`): rebase or cherry-pick the commit that added them onto a base/candidate that predates it.
- **`--replays DIR`** runs the exact thing instead: each recording played from tick 0 and cut off at harvest + warm +
  measure ticks (a debug `takclient` per build dir; the SIMPHASE lines before the harvest tick are dropped). Same report,
  no situation fidelity limit, roughly twice the wall of the situation mode and still about a third of a full-length
  replay round. Use it whenever a result will be quoted for a phase where the validation below shows a gap.
- **Identity is still the full replays** (`tools/legion_identity.sh`): a situation is not byte-identical to the recording
  and proves nothing about hashes.

### Validation (2026-10-10)

The situation ratios were checked against controlled truncated replays of the same pairs, same window, same quiet cores
(two rounds, four swapped pairs each); `Δ` is situation minus replay, in percentage points of the pair-median ratio.

Pair-median ratio of the combat phase, cand/base - 1, warm-up 1000 and 1000 measured ticks (replay window: the same ticks
of the recording). E1.3 (13bc9697 vs 20a5195a, findTarget gate order) and E2.1 (d361329a vs f0e49384, forEachNear block skip):

| pair | workload | replay | situation | delta (pp) |
|---|---|---|---|---|
| E2.1 | R-2h (10k) | -21.1 | -21.7 | -0.6 |
| E2.1 | L-2h (10k) | -19.0 | -19.7 | -0.6 |
| E2.1 | R-bench | -26.4 | -29.3 | -2.9 |
| E2.1 | L-bench | -26.2 | -29.4 | -3.3 |
| E1.3 | R-2h (10k) | -22.5 | -17.3 | +5.2 |
| E1.3 | R-bench | -35.7 | -33.7 | +2.0 |
| E1.3 | L-bench | -2.6 | -0.8 | +1.9 |
| E1.3 | **L-2h (10k)** | **-12.5** | **+1.9** | **+14.4** |

The other phases agree to within about 3 pp in most rows and 7 pp in all of them (`other` of E2.1 L-2h, `sep` and
`movement` of the E2.1 benches), where the replay pairs themselves spread by 5-15 pp. E2.1's `grid` phase on the benches
(0.015 ms) reads +36..+46% in the replays and +36..+38% in the situations.
Two cases do not meet the 3 pp bar, and why:

- **E1.3 on L-2h: the situation shows nothing where the replay shows -12..-14%.** The work counters match the recording
  closely (acq_scans, los_calls and the queue walks within 5%, order-queue shapes equal), so the missing gain is a
  cost-per-walk difference. Tested and ruled out: heap age (fragmenting the allocator before the build changed nothing), the
  Retail waypoint backlog (Legion has none), the warm-up (the ratio is flat from tick 0 to 2500). It is not understood; treat
  any Legion combat-phase claim from order-queue or acquisition changes as unconfirmed until `--replays` agrees.
- **E1.3 on R-2h: -17% against -22%.** Without `--catchup` it was -11% and still rising: the recording holds ~900 units
  with 8+ waypoint orders that a rebuilt world takes ~2000 ticks to regrow (the retail path-budget scheduler drains the
  tick-0 backlog slowly). `--catchup 60` brings most of them back at once; the rest of the gap (the waypoint queues
  plateau at ~85% of the recording's) is left.

Absolute ms are not comparable between modes (a situation tick ran from 18% cheaper to 4% dearer than the recording's, base
build), and the host differs between days, so compare only within one pair.
Round wall: 9-11 minutes for two swapped rounds of all four situations (sum of run times 8-10 min, plus the wait for the
timing cores), against ~70 minutes.

## Developer launch modes

Release clients accept `--data` and `--version` and launch games through the
menus. Debug builds also expose asset viewers, direct game/replay launch, and
headless diagnostic harnesses. See the [command-line reference](user-guide.md#command-line)
and `build-o2/takclient --help`; additional harness arguments are defined in
[src/client/main.cpp](../src/client/main.cpp). Client `TAK_*` diagnostic
environment hooks are disabled in Release builds.

## Server authentication

Sign-in uses SCRAM-SHA-256-style challenge/response over the game's binary
framing. The server stores salts and derived keys rather than passwords;
password derivation uses 600,000 PBKDF2-HMAC-SHA256 iterations. Authentication
does not encrypt game traffic, and first-time account registration has no
established server identity to authenticate against. See
[src/net/auth.h](../src/net/auth.h) for the protocol and its limits. Repeated
failures trigger account/address lockouts.

Accounts live in one plain-text file (`--accounts`, default
`takserver-accounts.conf`), written owner-read/write on POSIX and replaced atomically —
no database. New passwords must be at least 8 characters; names are 3-20
characters of letters, digits, `_`, `-` or `.`, unique case-insensitively.
`tools/authtest.cpp` checks the primitives against the published FIPS/RFC test
vectors and drives the exchange through replay, downgrade and stolen-file
attacks.

## Project layout

| Path | Contents |
| --- | --- |
| `src/hpi/` | HPI archive reader (TAK's revised format vs. classic TA) |
| `src/gaf/` | GAF/TAF sprite, animation, and font decoding |
| `src/video/` | `.bik` (Bink Video) decoding for the menu door clips (FFmpeg-backed) |
| `src/tnt/` | TNT map decoding |
| `src/tdo/` | 3DO model loading |
| `src/cob/` | COB script bytecode VM (unit animation/scripting) |
| `src/tdf/` | TDF/FBI/OTA text-config parsing |
| `src/crt/` | `.crt` scenario/trigger parsing |
| `src/campaign/` | campaign spine (`camps/*.tdf`) + in-sim mission/god-script runner |
| `src/sim/` | deterministic simulation (movement, pathfinding, combat, economy) |
| `src/net/` | multiplayer wire format, framed TCP, client protocol |
| `src/server/` | `takserver`, the headless lobby + lockstep relay |
| `src/ai/` | the skirmish AI (server-portable; emits commands) |
| `src/terrain/` | terrain / palette handling |
| `src/util/` | shared helpers |
| `src/gui/` | retail `.gui` HUD/gadget layout parsing |
| `src/client/` | the SDL2 app (`takclient`: asset viewer + game) |
| `src/cartographer/` | `cartographer`, a clean-room port of the retail map editor (in progress) |
| `tools/` | CLI dev tools (`hpitool`, `gaftool`, `tnttool`, `modeltool`, `cobtool`, `tdftool`, `missiontool`, `biktool`, `aitool`) |
| `docs/` | format notes + reverse-engineering findings (`retail-engine.md` = the `KINGDOMS.icd` disassembly) |
