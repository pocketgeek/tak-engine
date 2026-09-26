# Build and development guide

[Back to the README](../README.md) · [User guide](user-guide.md)

The engine version is set by `project(... VERSION ...)` in `CMakeLists.txt`.
It is separate from the multiplayer protocol version in `src/net/protocol.h`.

## Building

Use CMake ≥ 3.24, a C++20 compiler (GCC/Clang or MinGW-w64), Ninja, Git,
Make, and pkg-config. Linux and macOS builds require the vendored static
zlib, libjpeg-turbo, SDL2, and Bink-only FFmpeg libraries. Installing a system
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
The dependency script enables Bink decoding without GPL codecs.

SDL2, libjpeg, zlib, and FFmpeg are linked statically. Linux also embeds the
GCC/C++ runtime but retains system C libraries; SDL loads available windowing and
audio backends at runtime. Windows binaries need no separately bundled SDL,
JPEG, zlib, FFmpeg, or MinGW runtime DLLs. macOS uses system libraries and
frameworks. CI checks these dependency boundaries for release packages.

### Platform builds and releases

- **Windows x64:** use the MSYS2 **MINGW64** environment with its GCC, CMake,
  Ninja, SDL2, libjpeg-turbo, zlib, and pkgconf packages, plus Git and Make.
  Run `./tools/build-ffmpeg-bink.sh`, then configure and build as above.
  CMake uses the toolchain's static dependency archives; the native
  `build-static-deps.sh` step is not needed. See
  [windows.yml](../.github/workflows/windows.yml) for the exact package list and
  installer build.
- **macOS ARM64:** install Xcode Command Line Tools, then
  `brew install cmake ninja pkg-config`. Run both dependency scripts and the
  source-build commands above, adding `-DCMAKE_FIND_FRAMEWORK=LAST` at configure
  time. See [macos.yml](../.github/workflows/macos.yml) for app/DMG packaging.
- **Linux x64:** CI packages Ubuntu 22.04/24.04/26.04, Debian 12/13,
  Fedora 44, and Arch; use the package matching your distribution.

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
