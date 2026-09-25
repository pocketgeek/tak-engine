cmake_minimum_required(VERSION 3.24)
project(tak-engine VERSION 0.7.0 LANGUAGES CXX)

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

# --- zlib / libjpeg / SDL2 are ALWAYS linked statically ------------------------
# There is no switch for this. A dynamic build was only ever useful for bisecting a
# system-library bug, and keeping the option around meant the shipped configuration was
# not the one anyone built locally -- the shape of bug that reaches users. The binaries
# carry zlib/libjpeg/SDL2 internally, at pinned versions. (SDL2 still dlopens its
# X11/Wayland/audio backends, so THOSE system libs are still needed at runtime; that is
# SDL's design and not something linking can change.)
set(TAK_STATIC_DEPS_PREFIX "${CMAKE_SOURCE_DIR}/third_party/static-deps"
    CACHE PATH "Install prefix of the vendored static zlib/libjpeg/SDL2")
set(ZLIB_USE_STATIC_LIBS ON)                # FindZLIB honours this (CMake >= 3.24)
if(EXISTS "${TAK_STATIC_DEPS_PREFIX}/lib" OR EXISTS "${TAK_STATIC_DEPS_PREFIX}/lib64")
  # Vendored archives built by tools/build-static-deps.sh (Linux, macOS).
  list(PREPEND CMAKE_PREFIX_PATH "${TAK_STATIC_DEPS_PREFIX}")
  set(ZLIB_ROOT "${TAK_STATIC_DEPS_PREFIX}")
  set(JPEG_ROOT "${TAK_STATIC_DEPS_PREFIX}")   # only the .a lives there -> found static
  # DROP ANY CACHED DISCOVERY THAT POINTS OUTSIDE THE PREFIX. find_package does not
  # re-search once its cache entries are set, so a build directory configured before
  # these libraries became mandatory-static keeps pointing at the system's .so -- and
  # then links it while this file cheerfully prints "vendored from ...". The result is
  # the worst kind: a build that reports the configuration it does not have. A fresh
  # directory never showed it, which is why it needed review to find.
  foreach(_v ZLIB_LIBRARY ZLIB_LIBRARY_RELEASE ZLIB_LIBRARY_DEBUG ZLIB_INCLUDE_DIR
             JPEG_LIBRARY JPEG_LIBRARY_RELEASE JPEG_LIBRARY_DEBUG JPEG_INCLUDE_DIR
             SDL2_DIR SDL2_LIBRARY SDL2_INCLUDE_DIR)
    if(DEFINED CACHE{${_v}})
      set(_cached "$CACHE{${_v}}")
      string(FIND "${_cached}" "${TAK_STATIC_DEPS_PREFIX}" _inPrefix)
      if(NOT _inPrefix EQUAL 0)
        message(STATUS "Static libs: discarding stale ${_v} (${_cached})")
        unset(${_v} CACHE)
      endif()
    endif()
  endforeach()
  message(STATUS "Static libs: vendored from ${TAK_STATIC_DEPS_PREFIX}")
elseif(WIN32)
  # MSYS2/MinGW ships libz.a/libjpeg.a/libSDL2.a alongside the import libs. Prefer .a
  # over the .dll.a so find_package resolves the static ones.
  set(CMAKE_FIND_LIBRARY_SUFFIXES .a .dll.a ${CMAKE_FIND_LIBRARY_SUFFIXES})
  message(STATUS "Static libs: from the toolchain's static archives")
else()
  # Falling through here would silently find the system's SHARED SDL2 and produce a
  # dynamically linked binary that looks fine locally and fails the release gate -- or
  # worse, ships. Say what to run instead.
  message(FATAL_ERROR
    "The vendored static libraries are missing: ${TAK_STATIC_DEPS_PREFIX}\n"
    "  Build them once with:  ./tools/build-static-deps.sh\n"
    "(zlib, libjpeg-turbo and SDL2 are always linked statically; there is no dynamic "
    "build to fall back to.)")
endif()

# --- Format libraries (no external dependencies) ------------------------------

find_package(ZLIB REQUIRED)
find_package(JPEG REQUIRED)

add_library(tak-formats
  src/hpi/hpi.cpp
  src/gaf/gaf.cpp
  src/tnt/tnt.cpp
  src/tnt/ota.cpp
  src/tnt/mapgen.cpp
  src/tdo/tdo.cpp
  src/tdf/tdf.cpp
  src/cob/cob.cpp
  src/cob/vm.cpp
  src/sim/sim.cpp
  src/sim/pathsearch.cpp
  src/sim/detmath.cpp
  src/sim/matchsetup.cpp
  src/sim/mission.cpp
  src/sim/scenario.cpp
  src/campaign/campaign.cpp
  src/crt/crt.cpp
  src/gui/gui.cpp
  src/net/protocol.cpp
  src/net/conn.cpp
  src/net/client.cpp
  src/net/crypto.cpp
  src/net/auth.cpp
  src/util/png.cpp
  src/util/appicon.cpp
  src/util/jpeg.cpp
  src/terrain/terrain.cpp
)
target_include_directories(tak-formats PUBLIC src)
target_link_libraries(tak-formats PUBLIC ZLIB::ZLIB JPEG::JPEG)

# Inject the project version (src/version.h) into every target that links
# tak-formats -- one source of truth, set by project(... VERSION ...) above.
target_compile_definitions(tak-formats PUBLIC TAK_VERSION="${PROJECT_VERSION}")

# SOURCE fingerprint, separate from the release version. Two peers must run the same
# SIMULATION CODE to stay in lockstep, and nothing checked that: the gameplay-data hash
# covers data, kNetVersion covers the wire format, and neither moves when sim behaviour
# changes. A test server left one commit behind produced five "desyncs" that were
# nothing of the sort -- the placement code differed, so the worlds differed from tick
# zero. This is what tells those apart from a real divergence.
#
# The release version cannot serve here: it changes a handful of times a year, while
# the simulation changes daily.
#
# REGENERATED EVERY BUILD, never at configure time. Capturing it during cmake means a
# commit followed by an incremental build ships the PREVIOUS commit's id -- a stale
# fingerprint that would let the sweep's gate pass a mismatched pair, which is the
# whole failure it exists to prevent.
set(TAK_BUILD_ID_H "${CMAKE_BINARY_DIR}/generated/tak_build_id.h")
add_custom_target(tak_build_id ALL
    BYPRODUCTS "${TAK_BUILD_ID_H}"
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${TAK_BUILD_ID_H}
            -P "${CMAKE_SOURCE_DIR}/cmake/BuildId.cmake"
    COMMENT "Stamping build id")
# Seed it now so the first configure leaves a header to compile against.
execute_process(COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${TAK_BUILD_ID_H}
                        -P "${CMAKE_SOURCE_DIR}/cmake/BuildId.cmake")
target_include_directories(tak-formats PUBLIC "${CMAKE_BINARY_DIR}/generated")
add_dependencies(tak-formats tak_build_id)
target_compile_definitions(tak-formats PUBLIC TAK_HAVE_BUILD_ID_H=1)

# Windows sockets: src/net (in tak-formats) uses Winsock2 -- link ws2_32 so it
# propagates to takclient and takserver. No-op on POSIX.
if(WIN32)
  # bcrypt: BCryptGenRandom, the OS CSPRNG behind crypto::randomBytes.
  target_link_libraries(tak-formats PUBLIC ws2_32 bcrypt)
  # Fold the GCC/C++ runtime into each .exe so a Windows box needs no MinGW DLLs.
  # -static also pulls winpthread. SDL2/jpeg/zlib are always static too, so
  # nothing is shipped alongside -- windows.yml gates on the .exe importing no
  # SDL2/jpeg/zlib/FFmpeg DLL at all. (This comment used to say those three were still
  # shipped as DLLs, which stopped being true when that gate went in.)
  add_link_options(-static-libgcc -static-libstdc++ -static)
endif()

# Linux: fold the C++ runtime in too. SDL2/jpeg/zlib and the Bink FFmpeg are always
# static, which left libstdc++/libgcc_s/libatomic
# as the only non-libc dependencies -- and they are the ones that tie a package to the
# distro that built it, which is why the release matrix carries a per-distro tag and a
# gcc-12 variant.
#
# NOT -static. glibc resolves hosts through NSS modules it dlopens (the netcode calls
# getaddrinfo), and SDL2 deliberately dlopens its X11/Wayland/audio backends; a fully
# static binary would either fail to resolve or fail to open a window. libc and libm stay
# shared on purpose. This is the most static a Linux build can usefully be.
# Each flag is TESTED before it is used, rather than assumed: -static-libstdc++ needs
# libstdc++.a, which several distros split into a separate package (Fedora's
# libstdc++-static, and it is absent by default). Adding the flag blind turns a missing
# package into "cannot find -lstdc++" at link time, so a developer without it could no
# longer build at all. Tested, it simply stays dynamic there and the CI gate -- which
# installs the package -- is what holds the release to the static result.
if(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  include(CheckCXXSourceCompiles)
  function(tak_try_link_flag flag var)
    set(CMAKE_REQUIRED_LINK_OPTIONS ${flag})
    check_cxx_source_compiles("#include <string>\n#include <atomic>\nint main(){std::string s(\"x\");std::atomic<double> d{0};return int(s.size())+int(d.load());}" ${var})
    set(${var} "${${var}}" PARENT_SCOPE)
  endfunction()
  # A MISSING STATIC ARCHIVE IS A HARD FAILURE, not a quiet downgrade. Falling back to
  # the shared runtime produces a binary that looks fine, passes every test, and then
  # refuses to start on any machine whose libstdc++ is older than the build box -- the
  # exact failure this is meant to prevent, discovered by a user rather than by CI. The
  # probe exists to name the missing package, not to excuse it.
  tak_try_link_flag("-static-libgcc" TAK_HAVE_STATIC_LIBGCC)
  tak_try_link_flag("-static-libstdc++" TAK_HAVE_STATIC_LIBSTDCXX)
  if(NOT TAK_HAVE_STATIC_LIBGCC OR NOT TAK_HAVE_STATIC_LIBSTDCXX)
    message(FATAL_ERROR
      "The static C++ runtime is missing, so this build would depend on the host's "
      "libstdc++ version.\n"
      "  Fedora/RHEL:  sudo dnf install libstdc++-static\n"
      "  Debian/Ubuntu: sudo apt install libstdc++-*-dev  (ships libstdc++.a)\n"
      "  Arch:          already in the gcc package\n"
      "  Alpine:        apk add libstdc++-dev\n"
      "(-static-libgcc=${TAK_HAVE_STATIC_LIBGCC} -static-libstdc++=${TAK_HAVE_STATIC_LIBSTDCXX})")
  endif()
  add_link_options(-static-libgcc -static-libstdc++)
  # NOTE: libatomic is deliberately NOT handled here. add_link_options() places flags
  # BEFORE the object files, so a -Bstatic,-latomic there is processed while nothing has
  # referenced an atomic symbol yet and contributes nothing -- the plain -latomic that
  # FFmpeg's pkg-config adds AFTER the objects then binds the shared library anyway. It
  # is pinned where it is actually needed instead, inside the FFmpeg link group below.
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

add_executable(makeicons tools/makeicons.cpp)
target_link_libraries(makeicons PRIVATE tak-formats)

add_executable(gaftool tools/gaftool.cpp)
target_link_libraries(gaftool PRIVATE tak-formats)

add_executable(tnttool tools/tnttool.cpp)
target_link_libraries(tnttool PRIVATE tak-formats)

add_executable(missiontool tools/missiontool.cpp)
target_link_libraries(missiontool PRIVATE tak-formats)

add_executable(authtest tools/authtest.cpp src/server/accounts.cpp)
target_link_libraries(authtest PRIVATE tak-formats)

# Drives a real MpClient against a deliberately hostile server.
add_executable(authrogue tools/authrogue.cpp)
target_link_libraries(authrogue PRIVATE tak-formats)

add_executable(retailgap_test tools/retailgap_test.cpp)
add_executable(conjure_test tools/conjure_test.cpp)
add_executable(door_animation_test tools/door_animation_test.cpp)
target_include_directories(door_animation_test PRIVATE src)
add_executable(turn_test tools/turn_test.cpp)
add_executable(stuckrepro tools/stuckrepro.cpp)
add_executable(placement_test tools/placement_test.cpp)
add_executable(navblock_test tools/navblock_test.cpp)
add_executable(pathsearch_test tools/pathsearch_test.cpp)
add_executable(pathgeom_test tools/pathgeom_test.cpp)
add_executable(ambush_test tools/ambush_test.cpp)
add_executable(cadence_test tools/cadence_test.cpp)
add_executable(crowdbench tools/crowdbench.cpp)
add_executable(cobanim_test tools/cobanim_test.cpp)
add_executable(cmdflow_test tools/cmdflow_test.cpp)
add_executable(conn_test tools/conn_test.cpp)
add_executable(featgen_test tools/featgen_test.cpp)
add_executable(transport_test tools/transport_test.cpp)
add_executable(production_test tools/production_test.cpp)
add_executable(replay_test tools/replay_test.cpp)
add_executable(pathblock_test tools/pathblock_test.cpp)
add_executable(retail_motion_test tools/retail_motion_test.cpp)
add_executable(retail_script_test tools/retail_script_test.cpp)
target_link_libraries(retail_script_test PRIVATE tak-formats)
add_test(NAME retail_script COMMAND retail_script_test)
add_executable(weapon_hidden_release_test tools/weapon_hidden_release_test.cpp)
target_link_libraries(weapon_hidden_release_test PRIVATE tak-formats)
add_test(NAME weapon_hidden_release COMMAND weapon_hidden_release_test)
add_executable(retail_cost_test tools/retail_cost_test.cpp)
target_link_libraries(retail_cost_test PRIVATE tak-formats)
add_executable(retail_trace_test tools/retail_trace_test.cpp)
target_link_libraries(retail_trace_test PRIVATE tak-formats)
add_executable(retail_scheduler_test tools/retail_scheduler_test.cpp)
target_link_libraries(retail_scheduler_test PRIVATE tak-formats)
add_executable(retail_ai_test tools/retail_ai_test.cpp)
target_link_libraries(retail_ai_test PRIVATE tak-formats)
add_test(NAME retail_ai COMMAND retail_ai_test)
add_executable(retail_visual_test tools/retail_visual_test.cpp)
target_link_libraries(retail_visual_test PRIVATE tak-formats)
add_test(NAME retail_visual COMMAND retail_visual_test)
add_executable(retail_movement_animation_test tools/retail_movement_animation_test.cpp)
target_link_libraries(retail_movement_animation_test PRIVATE tak-formats)
add_test(NAME retail_movement_animation COMMAND retail_movement_animation_test)
add_executable(retail_builder_animation_test tools/retail_builder_animation_test.cpp)
target_link_libraries(retail_builder_animation_test PRIVATE tak-formats)
add_test(NAME retail_builder_animation COMMAND retail_builder_animation_test)
add_executable(nimbus_test tools/nimbus_test.cpp)
target_link_libraries(nimbus_test PRIVATE tak-formats)
add_test(NAME nimbus COMMAND nimbus_test)
add_executable(effect_inventory tools/effect_inventory.cpp)
target_link_libraries(effect_inventory PRIVATE tak-formats)
add_executable(retail_construction_test tools/retail_construction_test.cpp)
target_link_libraries(retail_construction_test PRIVATE tak-formats)
add_test(NAME retail_construction COMMAND retail_construction_test)
add_executable(retail_mission_test tools/retail_mission_test.cpp)
target_link_libraries(retail_mission_test PRIVATE tak-formats)
add_test(NAME retail_mission COMMAND retail_mission_test)
add_executable(retail_replay_probe tools/retail_replay_probe.cpp)
target_link_libraries(retail_replay_probe PRIVATE tak-formats)
target_link_libraries(retail_motion_test PRIVATE tak-formats)
add_test(NAME retail_motion COMMAND retail_motion_test)
add_executable(duration_test tools/duration_test.cpp)
add_executable(navincr_test tools/navincr_test.cpp)
add_executable(fixed_test tools/fixed_test.cpp)
add_executable(statsfit_test tools/statsfit_test.cpp)
add_executable(settings_test tools/settings_test.cpp)
    add_executable(footprobe tools/footprobe.cpp)
target_link_libraries(retailgap_test PRIVATE tak-formats)
target_link_libraries(conjure_test PRIVATE tak-formats)
target_link_libraries(turn_test PRIVATE tak-formats)
target_link_libraries(stuckrepro PRIVATE tak-formats)
target_link_libraries(placement_test PRIVATE tak-formats)
target_link_libraries(navblock_test PRIVATE tak-formats)
target_link_libraries(pathsearch_test PRIVATE tak-formats)
target_link_libraries(pathgeom_test PRIVATE tak-formats)
target_link_libraries(ambush_test PRIVATE tak-formats)
target_link_libraries(cadence_test PRIVATE tak-formats)
target_link_libraries(crowdbench PRIVATE tak-formats)
target_link_libraries(cobanim_test PRIVATE tak-formats)
target_link_libraries(cmdflow_test PRIVATE tak-formats)
target_link_libraries(conn_test PRIVATE tak-formats)
target_link_libraries(featgen_test PRIVATE tak-formats)
target_link_libraries(transport_test PRIVATE tak-formats)
target_link_libraries(production_test PRIVATE tak-formats)
target_link_libraries(replay_test PRIVATE tak-formats)
target_link_libraries(pathblock_test PRIVATE tak-formats)
target_link_libraries(duration_test PRIVATE tak-formats)
target_link_libraries(navincr_test PRIVATE tak-formats)
target_link_libraries(fixed_test PRIVATE tak-formats)
target_link_libraries(statsfit_test PRIVATE tak-formats)
target_link_libraries(settings_test PRIVATE tak-formats)

# ctest registration. Without this `ctest` reports "No tests were found", which
# is how pathsearch_test and cobanim_test ended up protected by nothing but
# someone remembering to run them.
#
# The split matters: the first group is self-contained and runs anywhere
# (including CI), the second needs a real retail install, which is gitignored
# and absent from CI by design. Configure with -DTAK_TEST_DATA=/path/to/install
# to register those too.
enable_testing()
add_test(NAME detmath COMMAND detmath_test)
add_test(NAME sim_driver_equiv COMMAND sim_driver_equiv_test)
add_test(NAME pathsearch COMMAND pathsearch_test)
add_test(NAME retail_scheduler COMMAND retail_scheduler_test)
add_test(NAME retail_trace COMMAND retail_trace_test --selftest)
add_test(NAME auth COMMAND authtest)
add_test(NAME cmdflow COMMAND cmdflow_test)
add_test(NAME conn COMMAND conn_test)
add_test(NAME featgen COMMAND featgen_test)
add_test(NAME transport COMMAND transport_test)
add_test(NAME explode_capture COMMAND cobanim_test --explode-capture)
add_test(NAME production COMMAND production_test)
add_test(NAME replay COMMAND replay_test)
add_test(NAME pathblock COMMAND pathblock_test)
add_test(NAME cadence COMMAND cadence_test)
add_test(NAME duration COMMAND duration_test)
add_test(NAME navincr COMMAND navincr_test)
add_test(NAME fixed COMMAND fixed_test)
add_test(NAME settings COMMAND settings_test)
add_test(NAME door_animation COMMAND door_animation_test)
add_test(NAME pathgeom COMMAND pathgeom_test)
add_test(NAME ambush COMMAND ambush_test)
add_executable(exploration_parallel_test tools/exploration_parallel_test.cpp)
target_link_libraries(exploration_parallel_test PRIVATE tak-formats)
add_test(NAME exploration_parallel COMMAND exploration_parallel_test)
add_test(NAME statsfit COMMAND statsfit_test)
add_executable(animation_roster_test tools/animation_roster_test.cpp)
target_link_libraries(animation_roster_test PRIVATE tak-formats)
add_executable(naval_combat_test tools/naval_combat_test.cpp)
target_link_libraries(naval_combat_test PRIVATE tak-formats)
add_executable(flyer_combat_test tools/flyer_combat_test.cpp)
target_link_libraries(flyer_combat_test PRIVATE tak-formats)
set(TAK_TEST_DATA "" CACHE PATH "Retail install for the data-backed tests (optional)")
if(TAK_TEST_DATA)
    add_test(NAME naval_combat COMMAND naval_combat_test "${TAK_TEST_DATA}")
    add_test(NAME transport_roster COMMAND transport_test "${TAK_TEST_DATA}")
    add_test(NAME transport_map_roundtrip COMMAND transport_test
        --surface-transport-map-roundtrip-type "${TAK_TEST_DATA}"
        "Lake Lokken" vertrans araarch 240 120 240 350)
    add_test(NAME transport_map_roundtrip_crusades COMMAND transport_test
        --surface-transport-map-roundtrip-type "${TAK_TEST_DATA}"
        "Lake Lokken" vertrans araarch 240 120 240 350 1)
    add_test(NAME animation_roster COMMAND animation_roster_test "${TAK_TEST_DATA}")
    add_test(NAME flyer_combat COMMAND flyer_combat_test "${TAK_TEST_DATA}")
    add_test(NAME gate_scripts COMMAND retail_script_test --gates "${TAK_TEST_DATA}")
    add_test(NAME cobanim COMMAND cobanim_test "${CMAKE_SOURCE_DIR}/assets/extracted/all/scripts")
    add_test(NAME retailgap COMMAND retailgap_test "${TAK_TEST_DATA}")
    add_test(NAME conjure COMMAND conjure_test "${TAK_TEST_DATA}")
    add_test(NAME turn COMMAND turn_test "${TAK_TEST_DATA}")
    add_test(NAME navblock COMMAND navblock_test "${TAK_TEST_DATA}")
    add_test(NAME placement COMMAND placement_test "${TAK_TEST_DATA}")
    # The Release client intentionally strips the local game/dev command path.
    # Keep this headless client integration fixture in Debug configurations.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_test(NAME script_transient COMMAND takclient game "ulasem arena"
            --data "${TAK_TEST_DATA}" --firetest --nofog --time 0.1
            --shot "${CMAKE_CURRENT_BINARY_DIR}/script-transient-test.png")
        set_tests_properties(script_transient PROPERTIES ENVIRONMENT
            "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;TAK_SCRIPT_TRANSIENT_TEST=1")
        add_test(NAME script_transient_263 COMMAND takclient game "Lake Lokken"
            --data "${TAK_TEST_DATA}" --firetest --nofog --time 0.1
            --shot "${CMAKE_CURRENT_BINARY_DIR}/script-transient-263-test.png")
        set_tests_properties(script_transient_263 PROPERTIES ENVIRONMENT
            "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;TAK_SCRIPT_263_TEST=1")
        add_test(NAME weapon_impact_effect_route COMMAND takclient game "Lake Lokken"
            --data "${TAK_TEST_DATA}" --firetest --nofog --time 0.1
            --shot "${CMAKE_CURRENT_BINARY_DIR}/weapon-impact-effect-test.png")
        set_tests_properties(weapon_impact_effect_route PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;TAK_WEAPON_IMPACT_EFFECT_TEST=1"
            PASS_REGULAR_EXPRESSION "PASS: Arapult land/water authored impact variants"
            TIMEOUT 120)
    endif()
endif()
target_link_libraries(footprobe PRIVATE tak-formats)

# aitool bundles the AI (src/ai, otherwise server-only) so we can tune it headless.
add_executable(ai_behavior_test tools/ai_behavior_test.cpp src/ai/ai.cpp)
target_link_libraries(ai_behavior_test PRIVATE tak-formats)
add_test(NAME ai_behavior COMMAND ai_behavior_test)
if(TAK_TEST_DATA)
    add_test(NAME ai_behavior_data COMMAND ai_behavior_test "${TAK_TEST_DATA}")
endif()

add_executable(aitool tools/aitool.cpp src/ai/ai.cpp)
target_link_libraries(aitool PRIVATE tak-formats)
add_executable(simperf tools/simperf.cpp src/ai/ai.cpp)
target_link_libraries(simperf PRIVATE tak-formats)

# --- Client (needs SDL2; skipped with a warning if unavailable) ---------------

option(TAK_BUILD_CLIENT "Build the SDL2 client (asset viewer + game)" ON)

# FFmpeg decodes the retail Bink (.bik) menu videos -- libavcodec carries a clean-room
# Bink decoder (LGPL). It is ALWAYS the minimal Bink-only static build from
# tools/build-ffmpeg-bink.sh; there is no dynamic link and no no-video build.
#
# The system libavcodec is never consulted, which is deliberate twice over: stock
# Fedora's libavcodec-free omits the Bink decoder entirely (so "it built" would not mean
# "videos play"), and its .pc files were the last place a shared libatomic could sneak
# back into an otherwise static binary. TAK_FFMPEG_PREFIX overrides where the static
# install lives.
set(TAK_FFMPEG_PREFIX "" CACHE PATH "Install prefix of the static Bink-only FFmpeg")

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  if(NOT TAK_FFMPEG_PREFIX)
    set(TAK_FFMPEG_PREFIX "${CMAKE_SOURCE_DIR}/third_party/ffmpeg-bink")
  endif()
  if(EXISTS "${TAK_FFMPEG_PREFIX}/lib/pkgconfig")
    set(ENV{PKG_CONFIG_PATH} "${TAK_FFMPEG_PREFIX}/lib/pkgconfig")
    # Order high-level -> low-level so the static archives resolve left-to-right.
    pkg_check_modules(FFMPEG libavformat libavcodec libswscale libswresample libavutil)
  else()
    message(FATAL_ERROR
      "The static Bink-only FFmpeg is missing: ${TAK_FFMPEG_PREFIX}\n"
      "  Build it once with:  ./tools/build-ffmpeg-bink.sh\n"
      "(FFmpeg is always linked statically; there is no dynamic build and no "
      "no-video build to fall back to.)")
  endif()
else()
  message(FATAL_ERROR "pkg-config is required to locate the static Bink-only FFmpeg")
endif()

# One INTERFACE target both takclient and biktool link.
if(FFMPEG_FOUND)
  add_library(tak_ffmpeg INTERFACE)
  target_compile_definitions(tak_ffmpeg INTERFACE TAK_HAVE_FFMPEG)
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
    #
    # FFmpeg's .pc files list a bare -latomic, which resolves to libatomic.so and is
    # the last non-libc dependency left once libstdc++ is static. Swap it for the
    # archive HERE, where it sits after the objects that need it -- the one position
    # where asking for the static form actually takes effect.
    set(_ff_ldflags ${FFMPEG_STATIC_LDFLAGS})
    if(UNIX AND NOT APPLE)
      list(TRANSFORM _ff_ldflags REPLACE "^-latomic$"
           "-Wl,--push-state,-Bstatic,-latomic,--pop-state")
    endif()
    target_link_libraries(tak_ffmpeg INTERFACE
      -Wl,--start-group ${_ff_ldflags} -Wl,--end-group)
  endif()
  message(STATUS "FFmpeg: STATIC Bink-only build from ${TAK_FFMPEG_PREFIX}")
endif()

if(TAK_BUILD_CLIENT)
  find_package(SDL2 QUIET)
  if(SDL2_FOUND)
    add_executable(takclient src/client/main.cpp src/client/gameview_impl.cpp src/client/gameview_render.cpp src/client/gameview_hud.cpp src/client/gameview_net.cpp src/client/gameview_lobby.cpp src/client/gameview_benchmark.cpp src/client/gameview_input.cpp src/client/gameview_unitinfo.cpp src/client/font.cpp src/client/mapview.cpp src/client/modelview.cpp src/client/replayfile.cpp src/client/mainmenu.cpp src/client/settings.cpp src/client/options.cpp src/client/hotkeys.cpp src/client/hotkeysscreen.cpp src/client/campaignscreen.cpp src/client/briefingscreen.cpp src/client/resultscreen.cpp src/client/loadscreen.cpp src/client/guiart.cpp src/client/cursors.cpp src/util/procmetrics.cpp src/video/bink.cpp)
    if(WIN32)
      target_link_libraries(takclient PRIVATE psapi)   # GetProcessMemoryInfo (benchmark metrics)
    elseif(APPLE)
      # procmetrics reads the accelerator's IORegistry PerformanceStatistics for
      # the benchmark's device-wide GPU% (no public per-process stat on macOS).
      target_link_libraries(takclient PRIVATE "-framework IOKit" "-framework CoreFoundation")
    endif()
    # The vendored static SDL2 exposes SDL2::SDL2-static (with its
    # transitive -ldl/-lm/-lpthread etc. baked into the target); the system SDL2 exposes
    # SDL2::SDL2. We provide our own main (SDL_MAIN_HANDLED), so SDL2main is never needed.
    if(TARGET SDL2::SDL2-static)
      set(_tak_sdl2 SDL2::SDL2-static)
    else()
      set(_tak_sdl2 SDL2::SDL2)
    endif()
    add_executable(cursor_test tools/cursor_test.cpp src/client/cursors.cpp)
    target_include_directories(cursor_test PRIVATE src)
    target_compile_definitions(cursor_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(cursor_test PRIVATE tak-formats ${_tak_sdl2})
    add_test(NAME cursor_roster COMMAND cursor_test)
    add_executable(shadow_test tools/shadow_test.cpp)
    target_include_directories(shadow_test PRIVATE src)
    target_compile_definitions(shadow_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(shadow_test PRIVATE ${_tak_sdl2})
    add_test(NAME shadow COMMAND shadow_test)
    add_executable(model_transform_test tools/model_transform_test.cpp)
    target_include_directories(model_transform_test PRIVATE src)
    target_compile_definitions(model_transform_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(model_transform_test PRIVATE ${_tak_sdl2})
    add_test(NAME model_transform COMMAND model_transform_test)
    # The retail-backed COB/3DO trace is an optional local regression. It uses
    # Unicorn and the extracted LIFBIRD assets, so keep it out of environments
    # that only have the C++ build dependencies installed.
    if(TAK_TEST_DATA AND EXISTS "${TAK_TEST_DATA}/KINGDOMS.icd" AND
       EXISTS "${CMAKE_SOURCE_DIR}/assets/extracted/all/scripts/lifbird.cob" AND
       EXISTS "${CMAKE_SOURCE_DIR}/assets/extracted/all/objects3d/lifbird.3do")
      find_package(Python3 COMPONENTS Interpreter QUIET)
      if(Python3_Interpreter_FOUND)
        execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import unicorn"
          RESULT_VARIABLE _tak_unicorn_status
          OUTPUT_QUIET ERROR_QUIET)
        if("${_tak_unicorn_status}" STREQUAL "0")
          add_test(NAME native_lifbird_ambient_pose
            COMMAND "${Python3_EXECUTABLE}"
              "${CMAKE_SOURCE_DIR}/tools/re/check_native_lifbird_ambient_pose.py"
              --script-binary "$<TARGET_FILE:retail_script_test>"
              --model-binary "$<TARGET_FILE:model_transform_test>"
              --icd "${TAK_TEST_DATA}/KINGDOMS.icd")
          set_tests_properties(native_lifbird_ambient_pose PROPERTIES TIMEOUT 60)
        endif()
      endif()
    endif()
    # ZONROC's mission-driven BeginFlight pose is separate from the generic
    # mover callback roster. Keep the retail COB/model join available when
    # the local ICD, extracted assets, and Unicorn emulator are installed.
    if(TAK_TEST_DATA AND EXISTS "${TAK_TEST_DATA}/KINGDOMS.icd" AND
       EXISTS "${CMAKE_SOURCE_DIR}/assets/extracted/all/scripts/zonroc.cob" AND
       EXISTS "${CMAKE_SOURCE_DIR}/assets/extracted/all/objects3d/zonroc.3do")
      find_package(Python3 COMPONENTS Interpreter QUIET)
      if(Python3_Interpreter_FOUND)
        execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import unicorn"
          RESULT_VARIABLE _tak_zonroc_unicorn_status
          OUTPUT_QUIET ERROR_QUIET)
        if("${_tak_zonroc_unicorn_status}" STREQUAL "0")
          add_test(NAME native_air_transport_animation_pose
            COMMAND "${Python3_EXECUTABLE}"
              "${CMAKE_SOURCE_DIR}/tools/re/check_native_air_transport_animation_pose.py"
              --script-binary "$<TARGET_FILE:retail_script_test>"
              --model-binary "$<TARGET_FILE:model_transform_test>"
              --icd "${TAK_TEST_DATA}/KINGDOMS.icd")
          set_tests_properties(native_air_transport_animation_pose PROPERTIES TIMEOUT 60)
        endif()
      endif()
    endif()
    # CREAERI has no BeginFlight/BeginLanding call-ins; its engine-driven
    # flight-to-ground mover edge animates the propellers and ground occupancy.
    if(TAK_TEST_DATA AND EXISTS "${TAK_TEST_DATA}/KINGDOMS.icd" AND
       EXISTS "${CMAKE_SOURCE_DIR}/assets/extracted/all/scripts/creaeri.cob" AND
       EXISTS "${CMAKE_SOURCE_DIR}/assets/extracted/all/objects3d/creaeri.3do")
      find_package(Python3 COMPONENTS Interpreter QUIET)
      if(Python3_Interpreter_FOUND)
        execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import unicorn"
          RESULT_VARIABLE _tak_creaeri_unicorn_status
          OUTPUT_QUIET ERROR_QUIET)
        if("${_tak_creaeri_unicorn_status}" STREQUAL "0")
          add_test(NAME native_creaeri_landing_pose
            COMMAND "${Python3_EXECUTABLE}"
              "${CMAKE_SOURCE_DIR}/tools/re/check_native_creaeri_landing_pose.py"
              --script-binary "$<TARGET_FILE:retail_script_test>"
              --model-binary "$<TARGET_FILE:model_transform_test>")
          set_tests_properties(native_creaeri_landing_pose PROPERTIES TIMEOUT 60)
        endif()
      endif()
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

    # Cartographer -- the clean-room map editor (docs/cartographer-port.md).
    # Shares the engine's terrain render stack (mapview + tak-formats) so maps
    # look byte-identical to the game.
    add_executable(cartographer src/cartographer/main.cpp src/cartographer/sections.cpp src/cartographer/newmap.cpp src/cartographer/features.cpp src/cartographer/units.cpp src/cartographer/triggers.cpp src/client/mapview.cpp)
    target_link_libraries(cartographer PRIVATE tak-formats ${_tak_sdl2})
    target_compile_definitions(cartographer PRIVATE SDL_MAIN_HANDLED)
    if(WIN32)
      target_link_options(cartographer PRIVATE -mconsole)
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
add_executable(takserver src/server/server.cpp src/server/accounts.cpp src/ai/ai.cpp)
target_link_libraries(takserver PRIVATE tak-formats)

add_executable(modeltool tools/modeltool.cpp)
target_link_libraries(modeltool PRIVATE tak-formats)

add_executable(tdftool tools/tdftool.cpp)
target_link_libraries(tdftool PRIVATE tak-formats)

add_executable(cobtool tools/cobtool.cpp)
target_link_libraries(cobtool PRIVATE tak-formats)

# --- Packaging: native .deb + .rpm via CPack (Linux) --------------------------
# One source of truth for both package families. Build, then from the build dir:
#   cpack -G DEB   -> tak-engine-<version>-amd64.deb   (sudo apt install ./tak-engine-*-amd64.deb)
#   cpack -G RPM   -> tak-engine-<version>-x86_64.rpm  (sudo dnf install ./tak-engine-*-x86_64.rpm)
# Every build bakes SDL2/jpeg/zlib and the Bink FFmpeg in, so SHLIBDEPS/AUTOREQ below
# auto-detect only the C runtime.
# SDL2 still dlopens its X11/Wayland/audio backends, which every desktop that can run the
# game already has; those are not declared (they aren't link-time deps of a static SDL2).
if(UNIX AND NOT APPLE)
  install(TARGETS takserver RUNTIME DESTINATION bin)
  if(TARGET takclient)
    install(TARGETS takclient RUNTIME DESTINATION bin)
  endif()
  if(TARGET cartographer)
    install(TARGETS cartographer RUNTIME DESTINATION bin)
  endif()

  # Desktop integration: the procedurally-drawn app icons (res/icons, generated
  # by `makeicons`) + .desktop launchers so the apps show a proper icon.
  install(FILES res/icons/takclient.png    DESTINATION share/icons/hicolor/256x256/apps RENAME tak-client.png)
  install(FILES res/icons/cartographer.png DESTINATION share/icons/hicolor/256x256/apps RENAME tak-cartographer.png)
  install(FILES res/icons/takserver.png    DESTINATION share/icons/hicolor/256x256/apps RENAME tak-server.png)
  install(FILES res/linux/tak-client.desktop
                res/linux/tak-cartographer.desktop
                res/linux/tak-server.desktop
          DESTINATION share/applications)

  set(CPACK_PACKAGE_NAME "tak-engine")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
      "Clean-room engine recreation of Total Annihilation: Kingdoms")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/pocketgeek/tak-engine")
  set(CPACK_PACKAGE_CONTACT "Curtis Edge <pocket_geek@pgnet.us>")
  set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

  set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")   # SPDX identifier
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

  # Version-stamped asset names (the download filename records the release); the CI
  # globs these when attaching to the release, and the README links to the releases
  # page rather than a fixed latest/download URL. TAK_DISTRO_TAG ("ubuntu22.04",
  # "fedora44", ...) is set by the per-distro CI matrix so each build's package
  # names the distro it was built on/for.
  set(TAK_DISTRO_TAG "" CACHE STRING "distro tag baked into package file names (e.g. ubuntu22.04)")
  set(_tak_dtag "")
  if(TAK_DISTRO_TAG)
    set(_tak_dtag "${TAK_DISTRO_TAG}-")
  endif()
  set(CPACK_DEBIAN_FILE_NAME "tak-engine-${PROJECT_VERSION}-${_tak_dtag}amd64.deb")
  set(CPACK_RPM_FILE_NAME "tak-engine-${PROJECT_VERSION}-${_tak_dtag}x86_64.rpm")

  # Auto-detect the shared-library dependencies for each family.
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_SECTION "games")
  set(CPACK_RPM_PACKAGE_AUTOREQ ON)
  set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")

  include(CPack)
elseif(WIN32)
  # Windows installer (NSIS via CPack; the CI's MSYS2 job runs `cpack -G NSIS`).
  # Binaries install into the chosen folder root (no bin/), with Start-menu
  # shortcuts and an uninstaller. The static build has no DLLs to carry.
  install(TARGETS takserver RUNTIME DESTINATION .)
  if(TARGET takclient)
    install(TARGETS takclient RUNTIME DESTINATION .)
  endif()

  set(CPACK_PACKAGE_NAME "tak-engine")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
      "Clean-room engine recreation of Total Annihilation: Kingdoms")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/pocketgeek/tak-engine")
  set(CPACK_PACKAGE_CONTACT "Curtis Edge <pocket_geek@pgnet.us>")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  set(CPACK_PACKAGE_FILE_NAME "tak-engine-${PROJECT_VERSION}-windows-x64-setup")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "TAK Engine")
  set(CPACK_NSIS_DISPLAY_NAME "TAK Engine")
  set(CPACK_NSIS_PACKAGE_NAME "TAK Engine")
  set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/pocketgeek/tak-engine")
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")        # shortcuts point at the install root
  set(CPACK_PACKAGE_EXECUTABLES "takclient" "TAK Engine")
  set(CPACK_NSIS_MUI_FINISHPAGE_RUN "takclient.exe")

  include(CPack)
endif()
