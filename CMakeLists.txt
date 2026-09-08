cmake_minimum_required(VERSION 3.24)
project(tak-engine VERSION 0.3.0 LANGUAGES CXX)

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
# door art. libavcodec carries a clean-room Bink decoder (GPL-compatible).
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(FFMPEG IMPORTED_TARGET libavcodec libavformat libavutil libswscale libswresample)
endif()

if(TAK_BUILD_CLIENT)
  find_package(SDL2 QUIET)
  if(SDL2_FOUND)
    add_executable(takclient src/client/main.cpp src/client/mainmenu.cpp src/client/settings.cpp src/client/options.cpp src/client/campaignscreen.cpp src/client/briefingscreen.cpp src/client/resultscreen.cpp src/client/cursors.cpp src/video/bink.cpp)
    target_link_libraries(takclient PRIVATE tak-formats SDL2::SDL2)
    # We provide our own main() and call SDL_SetMainReady(). Define SDL_MAIN_HANDLED for
    # the WHOLE target (not just main.cpp) so it applies before ANY header pulls in
    # <SDL.h> -- otherwise, on Windows, SDL_main.h renames our main to SDL_main via a
    # transitive include and the MinGW link fails with "undefined reference to WinMain".
    target_compile_definitions(takclient PRIVATE SDL_MAIN_HANDLED)
    if(FFMPEG_FOUND)
      target_link_libraries(takclient PRIVATE PkgConfig::FFMPEG)
      target_compile_definitions(takclient PRIVATE TAK_HAVE_FFMPEG)
      message(STATUS "FFmpeg found — Bink (.bik) menu videos enabled.")
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
if(FFMPEG_FOUND)
  add_executable(biktool tools/biktool.cpp src/video/bink.cpp)
  target_link_libraries(biktool PRIVATE tak-formats PkgConfig::FFMPEG)
  target_compile_definitions(biktool PRIVATE TAK_HAVE_FFMPEG)
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
# The runtime deps (SDL2/jpeg/zlib) are auto-detected so apt/dnf pull them in.
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
