cmake_minimum_required(VERSION 3.24)
project(tak-engine VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to an optimised build -- the engine renders/sims thousands of units, so
# -O0 Debug tanks the frame rate. Configure with -DCMAKE_BUILD_TYPE=Debug for a
# debug build.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# --- Optional: statically link vendored zlib / libjpeg / SDL2 -----------------
# -DTAK_STATIC_LIBS=ON links the static archives built by tools/build-static-deps.sh into
# TAK_STATIC_DEPS_PREFIX, so the binaries carry zlib/libjpeg/SDL2 internally -- no matching
# shared library needed at runtime, and the versions are pinned. (SDL2 still dlopens its
# X11/Wayland/audio backends, so THOSE system libs must still be present.) Default OFF is
# the normal system-shared build.
option(TAK_STATIC_LIBS "Statically link vendored zlib/libjpeg/SDL2 (tools/build-static-deps.sh)" OFF)
set(TAK_STATIC_DEPS_PREFIX "${CMAKE_SOURCE_DIR}/third_party/static-deps"
    CACHE PATH "Install prefix of the vendored static zlib/libjpeg/SDL2")
if(TAK_STATIC_LIBS)
  set(ZLIB_USE_STATIC_LIBS ON)              # FindZLIB honours this (CMake >= 3.24)
  if(EXISTS "${TAK_STATIC_DEPS_PREFIX}/lib" OR EXISTS "${TAK_STATIC_DEPS_PREFIX}/lib64")
    # Vendored archives built by tools/build-static-deps.sh (Linux, macOS).
    list(PREPEND CMAKE_PREFIX_PATH "${TAK_STATIC_DEPS_PREFIX}")
    set(ZLIB_ROOT "${TAK_STATIC_DEPS_PREFIX}")
    set(JPEG_ROOT "${TAK_STATIC_DEPS_PREFIX}")   # only the .a lives there -> found static
    message(STATUS "Static libs: vendored from ${TAK_STATIC_DEPS_PREFIX}")
  else()
    # No vendored prefix: use the toolchain's own static archives (e.g. MSYS2/MinGW ships
    # libz.a/libjpeg.a/libSDL2.a alongside the import libs). Prefer .a over the .dll.a
    # import libs so find_package resolves the static ones; SDL2::SDL2-static links static.
    if(WIN32)
      set(CMAKE_FIND_LIBRARY_SUFFIXES .a .dll.a ${CMAKE_FIND_LIBRARY_SUFFIXES})
    endif()
    message(STATUS "Static libs: from the toolchain's static archives")
  endif()
endif()

# --- Format libraries (no external dependencies) ------------------------------

find_package(ZLIB REQUIRED)
find_package(JPEG REQUIRED)

add_library(tak-formats
  src/hpi/hpi.cpp
  src/gaf/gaf.cpp
  src/tnt/tnt.cpp
  src/tdo/tdo.cpp
  src/tdf/tdf.cpp
  src/cob/cob.cpp
  src/cob/vm.cpp
  src/sim/sim.cpp
  src/sim/detmath.cpp
  src/sim/matchsetup.cpp
  src/sim/mission.cpp
  src/campaign/campaign.cpp
  src/crt/crt.cpp
  src/gui/gui.cpp
  src/net/protocol.cpp
  src/net/conn.cpp
  src/net/client.cpp
  src/util/png.cpp
  src/util/jpeg.cpp
  src/terrain/terrain.cpp
)
target_include_directories(tak-formats PUBLIC src)
target_link_libraries(tak-formats PUBLIC ZLIB::ZLIB JPEG::JPEG)

# Inject the project version (src/version.h) into every target that links
# tak-formats -- one source of truth, set by project(... VERSION ...) above.
target_compile_definitions(tak-formats PUBLIC TAK_VERSION="${PROJECT_VERSION}")

# Windows sockets: src/net (in tak-formats) uses Winsock2 -- link ws2_32 so it
# propagates to takclient and takserver. No-op on POSIX.
if(WIN32)
  target_link_libraries(tak-formats PUBLIC ws2_32)
  # Fold the GCC/C++ runtime into each .exe so a Windows box needs no MinGW DLLs
  # (only SDL2/jpeg/zlib remain, shipped alongside). -static also pulls winpthread.
  add_link_options(-static-libgcc -static-libstdc++ -static)
endif()

# Determinism: the simulation (src/sim) must produce bit-identical results across
# machines running the same binary for lockstep multiplayer. Disable floating-
# point contraction (FMA fuses a*b+c differently on different CPUs) and never
# enable -ffast-math on this library. See docs/multiplayer-design.md. (A fuller
# cross-build contract -- a deterministic sin/cos/atan2 shim -- comes later; this
# covers same-binary play, which is the v1 requirement.)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(tak-formats PRIVATE -ffp-contract=off)
endif()

# --- Command-line tools -------------------------------------------------------

add_executable(hpitool tools/hpitool.cpp)
target_link_libraries(hpitool PRIVATE tak-formats)

add_executable(detmath_test tools/detmath_test.cpp)
target_link_libraries(detmath_test PRIVATE tak-formats)

add_executable(sim_driver_equiv_test tools/sim_driver_equiv_test.cpp)
target_link_libraries(sim_driver_equiv_test PRIVATE tak-formats)

add_executable(gaftool tools/gaftool.cpp)
target_link_libraries(gaftool PRIVATE tak-formats)

add_executable(tnttool tools/tnttool.cpp)
target_link_libraries(tnttool PRIVATE tak-formats)

add_executable(missiontool tools/missiontool.cpp)
target_link_libraries(missiontool PRIVATE tak-formats)

# aitool bundles the AI (src/ai, otherwise server-only) so we can tune it headless.
add_executable(aitool tools/aitool.cpp src/ai/ai.cpp)
target_link_libraries(aitool PRIVATE tak-formats)

# --- Client (needs SDL2; skipped with a warning if unavailable) ---------------

option(TAK_BUILD_CLIENT "Build the SDL2 client (asset viewer + game)" ON)

# Optional FFmpeg for decoding the retail Bink (.bik) menu videos. When present,
# the main menu plays the door hover videos; when absent, it falls back to the GAF
# door art. libavcodec carries a clean-room Bink decoder (LGPL).
#
# Two ways to get it:
#   * Default: link the system FFmpeg dynamically (pkg-config). Needs a build whose
#     libavcodec includes the Bink decoder (stock Fedora's libavcodec-free does NOT;
#     RPM Fusion's does).
#   * -DTAK_STATIC_FFMPEG=ON: statically link a MINIMAL, Bink-only FFmpeg built by
#     tools/build-ffmpeg-bink.sh, so the shipped binary needs no runtime FFmpeg at all.
#     TAK_FFMPEG_PREFIX overrides where that static install lives.
option(TAK_STATIC_FFMPEG "Statically link a minimal Bink-only FFmpeg (tools/build-ffmpeg-bink.sh)" OFF)
set(TAK_FFMPEG_PREFIX "" CACHE PATH "Install prefix of the static Bink-only FFmpeg")

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  if(TAK_STATIC_FFMPEG)
    if(NOT TAK_FFMPEG_PREFIX)
      set(TAK_FFMPEG_PREFIX "${CMAKE_SOURCE_DIR}/third_party/ffmpeg-bink")
    endif()
    set(ENV{PKG_CONFIG_PATH} "${TAK_FFMPEG_PREFIX}/lib/pkgconfig")
    # Order high-level -> low-level so the static archives resolve left-to-right.
    pkg_check_modules(FFMPEG libavformat libavcodec libswscale libswresample libavutil)
  else()
    pkg_check_modules(FFMPEG IMPORTED_TARGET libavcodec libavformat libavutil libswscale libswresample)
  endif()
endif()

# One INTERFACE target both takclient and biktool link, hiding the static/dynamic split.
if(FFMPEG_FOUND)
  add_library(tak_ffmpeg INTERFACE)
  target_compile_definitions(tak_ffmpeg INTERFACE TAK_HAVE_FFMPEG)
  if(TAK_STATIC_FFMPEG)
    target_include_directories(tak_ffmpeg INTERFACE ${FFMPEG_STATIC_INCLUDE_DIRS})
    if(APPLE)
      # pkg-config lists macOS frameworks as the two tokens "-framework" "CoreVideo";
      # a plain link-libraries list would turn the bare "CoreVideo" into -lCoreVideo
      # (ld: library 'CoreVideo' not found). Re-pair each into a SHELL: link option so
      # both tokens reach the linker intact, and link the -L/-l rest normally. (Apple's
      # ld is multi-pass, so the circular archives need no --start-group.)
      set(_ff_rest "")
      list(LENGTH FFMPEG_STATIC_LDFLAGS _ff_n)
      set(_ff_i 0)
      while(_ff_i LESS _ff_n)
        list(GET FFMPEG_STATIC_LDFLAGS ${_ff_i} _ff_tok)
        if(_ff_tok STREQUAL "-framework")
          math(EXPR _ff_i "${_ff_i}+1")
          list(GET FFMPEG_STATIC_LDFLAGS ${_ff_i} _ff_fw)
          target_link_options(tak_ffmpeg INTERFACE "SHELL:-framework ${_ff_fw}")
        else()
          list(APPEND _ff_rest "${_ff_tok}")
        endif()
        math(EXPR _ff_i "${_ff_i}+1")
      endwhile()
      target_link_libraries(tak_ffmpeg INTERFACE ${_ff_rest})
    else()
      # --start/--end-group resolves the circular FFmpeg archives regardless of order
      # (GNU ld / MinGW).
      target_link_libraries(tak_ffmpeg INTERFACE
        -Wl,--start-group ${FFMPEG_STATIC_LDFLAGS} -Wl,--end-group)
    endif()
    message(STATUS "FFmpeg: STATIC Bink-only build from ${TAK_FFMPEG_PREFIX}")
  else()
    target_link_libraries(tak_ffmpeg INTERFACE PkgConfig::FFMPEG)
    message(STATUS "FFmpeg: system (dynamic) — Bink (.bik) menu videos enabled.")
  endif()
endif()

if(TAK_BUILD_CLIENT)
  find_package(SDL2 QUIET)
  if(SDL2_FOUND)
    add_executable(takclient src/client/main.cpp src/client/mainmenu.cpp src/client/settings.cpp src/client/options.cpp src/client/hotkeys.cpp src/client/hotkeysscreen.cpp src/client/campaignscreen.cpp src/client/briefingscreen.cpp src/client/resultscreen.cpp src/client/cursors.cpp src/util/procmetrics.cpp src/video/bink.cpp)
    if(WIN32)
      target_link_libraries(takclient PRIVATE psapi)   # GetProcessMemoryInfo (benchmark metrics)
    endif()
    # The vendored static SDL2 (TAK_STATIC_LIBS) exposes SDL2::SDL2-static (with its
    # transitive -ldl/-lm/-lpthread etc. baked into the target); the system SDL2 exposes
    # SDL2::SDL2. We provide our own main (SDL_MAIN_HANDLED), so SDL2main is never needed.
    if(TAK_STATIC_LIBS AND TARGET SDL2::SDL2-static)
      set(_tak_sdl2 SDL2::SDL2-static)
    else()
      set(_tak_sdl2 SDL2::SDL2)
    endif()
    target_link_libraries(takclient PRIVATE tak-formats ${_tak_sdl2})
    # We provide our own main() and call SDL_SetMainReady(). Define SDL_MAIN_HANDLED for
    # the WHOLE target (not just main.cpp) so it applies before ANY header pulls in
    # <SDL.h> -- otherwise, on Windows, SDL_main.h renames our main to SDL_main via a
    # transitive include and the MinGW link fails with "undefined reference to WinMain".
    target_compile_definitions(takclient PRIVATE SDL_MAIN_HANDLED)
    if(TARGET tak_ffmpeg)
      target_link_libraries(takclient PRIVATE tak_ffmpeg)
    else()
      message(STATUS "FFmpeg not found — menu .bik videos disabled (GAF door art fallback).")
    endif()
    if(WIN32)
      # SDL2::SDL2 drags in -mwindows (the GUI subsystem, whose CRT startup expects
      # WinMain). We deliberately keep our own console main() (SDL_MAIN_HANDLED, so no
      # SDL_main/WinMain), so force the console subsystem back on -- otherwise the
      # MinGW link fails with "undefined reference to WinMain".
      target_link_options(takclient PRIVATE -mconsole)
    endif()
  else()
    message(WARNING "SDL2 not found — skipping takclient. Install SDL2-devel.")
  endif()
endif()

# biktool: standalone Bink -> frame-dump debug tool (only when FFmpeg is present).
if(TARGET tak_ffmpeg)
  add_executable(biktool tools/biktool.cpp src/video/bink.cpp)
  target_link_libraries(biktool PRIVATE tak-formats tak_ffmpeg)
endif()

# --- Multiplayer server (headless; no SDL) ------------------------------------

# The skirmish AI (src/ai) is SERVER-ONLY -- it never links into takclient, so the
# client cannot run AI locally. AIs run only on the server.
add_executable(takserver src/server/server.cpp src/ai/ai.cpp)
target_link_libraries(takserver PRIVATE tak-formats)

add_executable(modeltool tools/modeltool.cpp)
target_link_libraries(modeltool PRIVATE tak-formats)

add_executable(tdftool tools/tdftool.cpp)
target_link_libraries(tdftool PRIVATE tak-formats)

add_executable(cobtool tools/cobtool.cpp)
target_link_libraries(cobtool PRIVATE tak-formats)

# --- Packaging: native .deb + .rpm via CPack (Linux) --------------------------
# One source of truth for both package families. Build, then from the build dir:
#   cpack -G DEB   -> tak-engine-amd64.deb    (sudo apt install ./tak-engine-amd64.deb)
#   cpack -G RPM   -> tak-engine-x86_64.rpm   (sudo dnf install ./tak-engine-x86_64.rpm)
# The release CI builds these with -DTAK_STATIC_LIBS=ON, so SDL2/jpeg/zlib (and the Bink
# FFmpeg) are baked in -- SHLIBDEPS/AUTOREQ below then auto-detect only the C/C++ runtime.
# SDL2 still dlopens its X11/Wayland/audio backends, which every desktop that can run the
# game already has; those are not declared (they aren't link-time deps of a static SDL2).
if(UNIX AND NOT APPLE)
  install(TARGETS takserver RUNTIME DESTINATION bin)
  if(TARGET takclient)
    install(TARGETS takclient RUNTIME DESTINATION bin)
  endif()

  set(CPACK_PACKAGE_NAME "tak-engine")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
      "Clean-room engine recreation of Total Annihilation: Kingdoms")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/pocketgeek/tak-engine")
  set(CPACK_PACKAGE_CONTACT "Curtis Edge <pocket_geek@pgnet.us>")
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

  set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")   # SPDX identifier
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

  # Stable, version-less asset names so the README's releases/latest/download links
  # stay valid across versions (the package's internal version is ${PROJECT_VERSION}).
  set(CPACK_DEBIAN_FILE_NAME "tak-engine-amd64.deb")
  set(CPACK_RPM_FILE_NAME "tak-engine-x86_64.rpm")

  # Auto-detect the shared-library dependencies for each family.
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_SECTION "games")
  set(CPACK_RPM_PACKAGE_AUTOREQ ON)
  set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")

  include(CPack)
endif()
