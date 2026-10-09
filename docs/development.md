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
