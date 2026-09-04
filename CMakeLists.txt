cmake_minimum_required(VERSION 3.24)
project(tak-engine CXX)

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
  src/ai/ai.cpp
  src/crt/crt.cpp
  src/net/lockstep.cpp
  src/net/protocol.cpp
  src/net/conn.cpp
  src/util/png.cpp
  src/util/jpeg.cpp
  src/terrain/terrain.cpp
)
target_include_directories(tak-formats PUBLIC src)
target_link_libraries(tak-formats PUBLIC ZLIB::ZLIB JPEG::JPEG)

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

add_executable(gaftool tools/gaftool.cpp)
target_link_libraries(gaftool PRIVATE tak-formats)

add_executable(tnttool tools/tnttool.cpp)
target_link_libraries(tnttool PRIVATE tak-formats)

# --- Viewer (needs SDL2; skipped with a warning if unavailable) ---------------

option(TAK_BUILD_VIEWER "Build the SDL2 asset viewer" ON)

if(TAK_BUILD_VIEWER)
  find_package(SDL2 QUIET)
  if(SDL2_FOUND)
    add_executable(takview src/viewer/main.cpp)
    target_link_libraries(takview PRIVATE tak-formats SDL2::SDL2)
  else()
    message(WARNING "SDL2 not found — skipping takview. Install SDL2-devel.")
  endif()
endif()

# --- Multiplayer server (headless; no SDL) ------------------------------------

add_executable(takserver src/server/server.cpp)
target_link_libraries(takserver PRIVATE tak-formats)

add_executable(modeltool tools/modeltool.cpp)
target_link_libraries(modeltool PRIVATE tak-formats)

add_executable(tdftool tools/tdftool.cpp)
target_link_libraries(tdftool PRIVATE tak-formats)

add_executable(cobtool tools/cobtool.cpp)
target_link_libraries(cobtool PRIVATE tak-formats)
