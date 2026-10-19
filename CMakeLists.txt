cmake_minimum_required(VERSION 3.24)
project(tak-engine VERSION 0.8.5 LANGUAGES CXX)

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

# Unoptimized builds (no -O1..-O3/-Os/-Oz/-Ofast in the configuration's C++ flags, as in a
# plain Debug build) and sanitizer builds run the simulation many times slower. A few long
# tests get a larger TIMEOUT there (TAK_SLOW_TEST_BUILD); optimized builds keep the tighter
# limits, which still catch a test that slows down.
string(TOUPPER "${CMAKE_BUILD_TYPE}" _tak_build_type_upper)
set(_tak_test_flags " ${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_tak_build_type_upper}} ")
if(_tak_test_flags MATCHES "-fsanitize" OR NOT _tak_test_flags MATCHES " -O([1-3sz]|fast) ")
  set(TAK_SLOW_TEST_BUILD ON)
else()
  set(TAK_SLOW_TEST_BUILD OFF)
endif()

# Build speed (developer machines only; neither tool changes the code generated).
# ccache: many worktrees build the same sources, so a shared compiler cache turns a
# fresh build into mostly cache hits. Worktrees only share entries when ccache's
# base_dir covers their paths (base_dir = / and hash_dir = false in ccache.conf).
# mold: the build links ~300 executables against large static libraries, which GNU
# ld spends much of an incremental build on. Both are used when found and can be
# turned off; CI and release builds without them are unaffected.
option(TAK_CCACHE "Use ccache as the compiler launcher when it is installed" ON)
option(TAK_MOLD "Link with mold on Linux when it is installed" ON)
if(TAK_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
  find_program(TAK_CCACHE_PROGRAM ccache)
  if(TAK_CCACHE_PROGRAM)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${TAK_CCACHE_PROGRAM}")
  endif()
endif()
if(TAK_MOLD AND UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  find_program(TAK_MOLD_PROGRAM mold)
  if(TAK_MOLD_PROGRAM)
    add_link_options(-fuse-ld=mold)
  endif()
endif()

# Determinism: no floating-point contraction ANYWHERE. A contracted a*b+c is one
# fused multiply-add (one rounding) instead of two, so the same source gives a
# different last bit wherever the compiler fuses. GCC on baseline x86-64 has no FMA
# instruction to fuse into, but Apple Clang on arm64 contracts by default
# (-ffp-contract=on), and a fused last bit is enough to split lockstep.
#
# This used to be set on tak-formats alone, which left everything compiled into the
# executables contracting on a Mac: the server's AI (src/ai/ai.cpp, part of
# takserver), whose `factory.z + sin(a)*r` door-clearing Move came out one ulp
# different on macOS arm64 and made every Mac-hosted Legion game diverge from the
# same game hosted on Linux/Windows from tick 3271 of the Inner Circle --mpai run;
# and every sim header inlined into client/server code (retailaim.h & co.), where
# the linker may keep the contracted copy of an inline function for the sim too.
# Hence a directory-wide option, not a per-target one: a new target or source file
# cannot opt out by forgetting. fp_contract_test proves it took effect.
# Never enable -ffast-math either. See docs/multiplayer-design.md.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-ffp-contract=off)
endif()

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
find_package(Threads REQUIRED)

add_library(tak-crypto STATIC src/net/crypto.cpp)
target_include_directories(tak-crypto PUBLIC src)
if(WIN32)
  target_link_libraries(tak-crypto PUBLIC bcrypt)
endif()

add_library(tak-formats
  src/hpi/hpi.cpp
  src/gaf/gaf.cpp
  src/tnt/tnt.cpp
  src/tnt/ota.cpp
  src/tnt/mapgen.cpp
  src/tnt/mapgen_balanced.cpp
  src/tdo/tdo.cpp
  src/tdf/tdf.cpp
  src/cob/cob.cpp
  src/cob/vm.cpp
  src/sim/sim.cpp
  src/sim/simpool.cpp
  src/sim/pathsearch.cpp
  src/sim/legion.cpp
  src/sim/convoy.cpp
  src/sim/detmath.cpp
  src/sim/matchsetup.cpp
  src/sim/mission.cpp
  src/sim/scenario.cpp
  src/sim/scenarioinfo.cpp
  src/campaign/campaign.cpp
  src/crt/crt.cpp
  src/gui/gui.cpp
  src/net/protocol.cpp
  src/net/matchconfig.cpp
  src/net/crusades.cpp
  src/net/conn.cpp
  src/net/client.cpp
  src/net/replaywriter.cpp
  src/net/mappackage.cpp
  src/net/overridepackage.cpp
  src/net/auth.cpp
  src/util/png.cpp
  src/util/appicon.cpp
  src/util/jpeg.cpp
  src/terrain/terrain.cpp
)
target_include_directories(tak-formats PUBLIC src)
target_link_libraries(tak-formats PUBLIC ZLIB::ZLIB JPEG::JPEG tak-crypto Threads::Threads)

add_library(tak-crusades-map STATIC src/client/crusadesmap.cpp)
target_link_libraries(tak-crusades-map PUBLIC tak-formats)
add_executable(crusades_presentation_test tools/crusades_presentation_test.cpp)
target_link_libraries(crusades_presentation_test PRIVATE tak-crusades-map)
add_test(NAME crusades_presentation COMMAND crusades_presentation_test)

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
  target_link_libraries(tak-formats PUBLIC ws2_32 shell32)
  # Fold the GCC/C++ runtime into each .exe so a Windows box needs no MinGW DLLs.
  # -static also pulls winpthread. SDL2/jpeg/zlib are always static too, so
  # nothing is shipped alongside -- windows.yml gates on the .exe importing no
  # SDL2/jpeg/zlib/FFmpeg DLL at all. (This comment used to say those three were still
  # shipped as DLLs, which stopped being true when that gate went in.)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_link_options(-static-libgcc -static-libstdc++)
  endif()
  # CLANGARM64 uses LLVM compiler-rt/libc++; -static folds those in too.
  add_link_options(-static)
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
# package into "cannot find -lstdc++" at link time. The probes fail configuration
# with an explanation of the missing package; they never select a shared fallback.
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

# --- Command-line tools -------------------------------------------------------

add_executable(hpitool tools/hpitool.cpp)
target_link_libraries(hpitool PRIVATE tak-formats)

add_executable(crusades_balance_audit tools/crusades_balance_audit.cpp)
target_link_libraries(crusades_balance_audit PRIVATE tak-formats)

add_executable(detmath_test tools/detmath_test.cpp)
target_link_libraries(detmath_test PRIVATE tak-formats)

add_executable(fp_contract_test tools/fp_contract_test.cpp)

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
add_executable(death_lifecycle_test tools/death_lifecycle_test.cpp)
target_link_libraries(death_lifecycle_test PRIVATE tak-formats)
add_executable(building_death_test tools/building_death_test.cpp)
target_link_libraries(building_death_test PRIVATE tak-formats)
add_executable(clear_build_test tools/clear_build_test.cpp)
target_link_libraries(clear_build_test PRIVATE tak-formats)
add_executable(lodestone_upgrade_test tools/lodestone_upgrade_test.cpp)
target_link_libraries(lodestone_upgrade_test PRIVATE tak-formats)
add_executable(builder_automation_test tools/builder_automation_test.cpp)
target_link_libraries(builder_automation_test PRIVATE tak-formats)
add_test(NAME builder_automation COMMAND builder_automation_test)
add_executable(fire_sweep_test tools/fire_sweep_test.cpp)
target_link_libraries(fire_sweep_test PRIVATE tak-formats)
add_test(NAME fire_sweep_vision COMMAND fire_sweep_test vision)
add_test(NAME fire_sweep_switch COMMAND fire_sweep_test switch)
add_test(NAME fire_sweep_retaliation COMMAND fire_sweep_test retaliation)
set_tests_properties(builder_automation PROPERTIES TIMEOUT 180)
add_executable(door_animation_test tools/door_animation_test.cpp)
target_include_directories(door_animation_test PRIVATE src)
add_executable(turn_test tools/turn_test.cpp)
add_executable(stuckrepro tools/stuckrepro.cpp)
add_executable(placement_test tools/placement_test.cpp)
add_executable(navblock_test tools/navblock_test.cpp)
add_executable(reclaimarea_test tools/reclaimarea_test.cpp)
add_executable(campaign_test tools/campaign_test.cpp)
add_executable(campaign_conditions_test tools/campaign_conditions_test.cpp)
add_executable(pathfinding_matchsetup_test tools/pathfinding_matchsetup_test.cpp)
target_link_libraries(pathfinding_matchsetup_test PRIVATE tak-formats)
add_test(NAME pathfinding_matchsetup COMMAND pathfinding_matchsetup_test)
add_executable(start_positions_test tools/start_positions_test.cpp)
target_link_libraries(start_positions_test PRIVATE tak-formats)
add_test(NAME start_positions COMMAND start_positions_test)

add_executable(pathsearch_test tools/pathsearch_test.cpp)
add_executable(pathgeom_test tools/pathgeom_test.cpp)
add_executable(ambush_test tools/ambush_test.cpp)
add_executable(cadence_test tools/cadence_test.cpp)
add_executable(crowdbench tools/crowdbench.cpp)
add_executable(navigation_telemetry_test tools/navigation_telemetry_test.cpp)
target_link_libraries(navigation_telemetry_test PRIVATE tak-formats)
add_test(NAME navigation_telemetry COMMAND navigation_telemetry_test)
add_executable(legion_world_test tools/legion_world_test.cpp)
target_link_libraries(legion_world_test PRIVATE tak-formats)
foreach(legion_case clearance groupreuse jagged trapped longroute crowdhold replace unreachable quota pens determinism formation
    slotblock deaths deathsshared splitgoal farclick churn approachhold approachopen churnfield approachchurn legacyyield planeincremental planeprebuild penstale lattice wallend staticblock
    navalclearance navalisland hovershore navalmissions squadformation pinwheel landedflyers mixedformation flyerfront liftflyers aware b3events lanegeom
    lonetrees slideback stillturn forestedge mixedsettle entryplanes)
  add_test(NAME legion_${legion_case} COMMAND legion_world_test ${legion_case})
  # TAK_LEGION_VERIFY (debug builds; compiled out of Release): every derived index, work list
  # and the incrementally kept soft state (W4 B2's softHash, C31) against a recompute, each tick.
  # Costs these 39 cases ~17 s -> ~44 s in total at -O2 (the longest, squadformation, 6 -> 11 s).
  set_tests_properties(legion_${legion_case} PROPERTIES LABELS legion ENVIRONMENT TAK_LEGION_VERIFY=1)
endforeach()
# planeincremental also runs its churn with workers (three in debug builds) and requires serial == workers.
set_tests_properties(legion_planeincremental PROPERTIES ENVIRONMENT "TAK_LEGION_VERIFY=1;TAK_SIM_WORKERS=3")
# W6 step 0 (PLAN 4 W6): the flyer instruments of tools/legion_flyers_test.cpp. Each prints the numbers the W6
# steps move and asserts determinism (serial == workers); W6_REQUIRE=1 asserts the PLAN targets (the acceptance
# check of the step that meets them; docs/legion-w6-step0.md).
add_executable(legion_flyers_test tools/legion_flyers_test.cpp)
target_link_libraries(legion_flyers_test PRIVATE tak-formats)
foreach(legion_probe mixedsquad mixedseal mv08group mv08deadend deadendrejoin ctrlmixed ctrlmixedbig factoryjoin auditlift
    fl04 fl05match fl05unreach predicates)
  add_test(NAME legion_flyers_${legion_probe} COMMAND legion_flyers_test ${legion_probe})
  set_tests_properties(legion_flyers_${legion_probe} PROPERTIES LABELS "legion;w6")
endforeach()
# The lift and touchdown cases also run with TAK_LEGION_VERIFY (debug builds): the lift buckets and homes against a scan each tick.
foreach(legion_probe landunder descentwalkin auditlift40 splitpatrol liftcombat)
  add_test(NAME legion_flyers_${legion_probe} COMMAND legion_flyers_test ${legion_probe})
  set_tests_properties(legion_flyers_${legion_probe} PROPERTIES LABELS "legion;w6" ENVIRONMENT TAK_LEGION_VERIFY=1)
endforeach()
# W7 step 0 fixtures (PLAN 4 W7 step 0, docs/legion-exit-tables.md "W7 step 0"): the promoted RB-02 probe (ring clot,
# Holding-queue corridor, plug) and the awareness stream fixtures awarebig (768x768), awaredense (200 v 200 at 85% fill),
# crossthree and crosslong. Measurement only: each prints the numbers W7's steps move (done tick, standing share,
# contacts, hold_max, the aware / parting / give-way counters, the route-behind metric) in both awareness arms and asserts
# determinism (serial == workers) on the cheap rows; the bounds come with W7's steps.
foreach(legion_probe rb02 awarebig awaredense crossthree crosslong)
  add_test(NAME legion_w7_${legion_probe} COMMAND legion_world_test ${legion_probe})
  set_tests_properties(legion_w7_${legion_probe} PROPERTIES LABELS "legion;w7")
endforeach()
# T1 probes for W3 (measurement only; they assert determinism, not bounds).
foreach(legion_probe settlelatency doorplug)
  add_test(NAME legion_${legion_probe} COMMAND legion_world_test ${legion_probe})
  set_tests_properties(legion_${legion_probe} PROPERTIES LABELS legion)
endforeach()
# W2 step 0 probes (AR-03 structsquad / factorysquad, RB-04 patrolrepair): measurement only; they
# print the values the W2 steps move and assert determinism, not the targets.
foreach(legion_probe structsquad factorysquad patrolrepair)
  add_test(NAME legion_${legion_probe} COMMAND legion_world_test ${legion_probe})
  set_tests_properties(legion_${legion_probe} PROPERTIES LABELS "legion;w2")
endforeach()
# W4: staticidle -- parked groups beside continuous construction do no refresh work (B1, demand-driven
# refresh); it failed by construction on the step-0 head.
add_test(NAME legion_staticidle COMMAND legion_world_test staticidle)
set_tests_properties(legion_staticidle PROPERTIES LABELS "legion;w4")
# W4 C1: a paused first build resumes for a member's demand within 10 ticks.
add_test(NAME legion_pausedemand COMMAND legion_world_test pausedemand)
set_tests_properties(legion_pausedemand PROPERTIES LABELS "legion;w4")
# Legion 10,000 stage 4: more mobility classes than kMaxPlanes start together; no plane is evicted before
# the registrations waiting for it use it (it failed on the pre-review head: 36 builds for 25 classes).
add_test(NAME legion_planepin COMMAND legion_world_test planepin)
set_tests_properties(legion_planepin PROPERTIES LABELS "legion")
# Legion 10,000 stage 6: tile chains (transit tiles shared across destinations, the descent check; with a wall
# dropped and lifted while they build) and a reused plane slot never reading the previous class's tiles; all
# under TAK_LEGION_VERIFY.
foreach(legion_case chains chainschurn chainplane)
  add_test(NAME legion_${legion_case} COMMAND legion_world_test ${legion_case})
  set_tests_properties(legion_${legion_case} PROPERTIES LABELS "legion" ENVIRONMENT TAK_LEGION_VERIFY=1)
endforeach()
# Legion 10,000 stage 7 (static navigation apart from traffic): an improvement defers its route's refresh while a
# closure refreshes at once (improvedefer); a chain relabelled while it builds re-resolves its regions instead of
# leaving them off (chainresolve); the coarse search routes round a region full of idle bodies (trafficlane); and
# (trapped) a trapped order retires under never-ending churn elsewhere, all under TAK_LEGION_VERIFY.
foreach(legion_case improvedefer chainresolve trafficlane)
  add_test(NAME legion_${legion_case} COMMAND legion_world_test ${legion_case})
  set_tests_properties(legion_${legion_case} PROPERTIES LABELS "legion" ENVIRONMENT TAK_LEGION_VERIFY=1)
endforeach()
foreach(legion_probe pocket deadend tail)
  add_test(NAME legion_${legion_probe} COMMAND legion_world_test ${legion_probe})
  set_tests_properties(legion_${legion_probe} PROPERTIES LABELS "legion;nightly" TIMEOUT 1200)
endforeach()
# Legion's headway window (World::updateLegionStill, LegionHeadway) on made-up motion: the 0.8.1
# walk-in-place stands, genuine slow motion never does.
add_executable(legion_still_test tools/legion_still_test.cpp)
target_link_libraries(legion_still_test PRIVATE tak-formats)
add_test(NAME legion_still COMMAND legion_still_test)
set_tests_properties(legion_still PROPERTIES LABELS legion)
# The const scenario observer (tools/legion_observe.h) on hand-placed bodies.
add_executable(legion_observe_selftest tools/legion_observe_selftest.cpp)
target_link_libraries(legion_observe_selftest PRIVATE tak-formats)
add_test(NAME legion_observe_selftest COMMAND legion_observe_selftest)
set_tests_properties(legion_observe_selftest PROPERTIES LABELS legion)
# .scn scenarios and issueSelection (the client's 64-per-tick order split).
add_executable(navbudget_test tools/navbudget_test.cpp)
target_include_directories(navbudget_test PRIVATE src)
add_test(NAME navbudget_test COMMAND navbudget_test)
set_tests_properties(navbudget_test PROPERTIES LABELS legion)
# Legion 10,000 stage 6: the routing hierarchy (src/sim/legion_hier.h), header-only.
add_executable(legion_hier_test tools/legion_hier_test.cpp)
target_include_directories(legion_hier_test PRIVATE src)
add_test(NAME legion_hier_test COMMAND legion_hier_test)
set_tests_properties(legion_hier_test PROPERTIES LABELS legion)
# Legion 10,000 stage 5: the route-query interface (src/sim/legion_route.h), header-only.
add_executable(legion_route_test tools/legion_route_test.cpp)
target_include_directories(legion_route_test PRIVATE src)
add_test(NAME legion_route_test COMMAND legion_route_test)
set_tests_properties(legion_route_test PROPERTIES LABELS legion)
add_executable(convoy_test tools/convoy_test.cpp)
target_link_libraries(convoy_test PRIVATE tak-formats)
add_test(NAME convoy_test COMMAND convoy_test)
set_tests_properties(convoy_test PROPERTIES LABELS legion)
# IdMap (sim/idmap.h): the flat groups/members/anchors maps visit as std::map.
add_executable(legion_idmap_test tools/legion_idmap_test.cpp)
target_include_directories(legion_idmap_test PRIVATE src)
add_test(NAME legion_idmap_test COMMAND legion_idmap_test)
set_tests_properties(legion_idmap_test PROPERTIES LABELS legion)

add_executable(selportraits_test tools/selportraits_test.cpp)   # header-only client logic, no SDL
target_include_directories(selportraits_test PRIVATE src)
add_test(NAME selportraits COMMAND selportraits_test)

add_executable(issue_selection_test tools/issue_selection_test.cpp)
target_link_libraries(issue_selection_test PRIVATE tak-formats)
add_test(NAME issue_selection_test COMMAND issue_selection_test)
set_tests_properties(issue_selection_test PROPERTIES LABELS legion)
# The scenario runner: a .scn file in both modes, serial and workers, three
# start offsets, observer keys as JSON (tools/legion_scenario.cpp). Exit 77 =
# the file needs a retail install (--data).
add_executable(legion_scenario tools/legion_scenario.cpp)
target_link_libraries(legion_scenario PRIVATE tak-formats)
# scn_truth: the situation fidelity check alone (no observer), small enough to build in the old trees that
# recorded the user's replays (see tools/scn_truth.cpp).
add_executable(scn_truth tools/scn_truth.cpp)
target_link_libraries(scn_truth PRIVATE tak-formats)
add_test(NAME legion_scenario_smoke
         COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/smoke-open.scn --mode both --json)
add_test(NAME observer_neutral
         COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/smoke-open.scn builtin:formation
                 --neutral --mode both --offsets 0)
set_tests_properties(legion_scenario_smoke observer_neutral
                     PROPERTIES SKIP_RETURN_CODE 77 LABELS legion)
# The Legion 10,000 scale probe (tools/legion_scale.h, `probe scale`): it changes no hash with the observer on
# or off, serial or workers (legion_scale_neutral), and the smallest scale file still delivers every body
# (legion_scale_one100: all 100 arrive, none stuck). The suite itself (tools/scenarios/scale/, 100 to 10,000
# bodies) is run by tools/legion_scale.py at stage ends, not by ctest (docs/legion-10000-progress.md).
add_test(NAME legion_scale_neutral
         COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/scale/one-100.scn
                 ${CMAKE_SOURCE_DIR}/tools/scenarios/scale/churn-1000.scn --neutral --mode both --offsets 0)
add_test(NAME legion_scale_one100
         COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/scale/one-100.scn --mode legion --offsets 0
                 --serial --json)
set_tests_properties(legion_scale_neutral legion_scale_one100 PROPERTIES LABELS legion TIMEOUT 600)
if(TAK_SLOW_TEST_BUILD)
  # Unoptimized, run alone (2026-10-18): 609 s Clang Debug, 486 s GCC -O0, 365 s ASan.
  set_tests_properties(legion_scale_neutral PROPERTIES TIMEOUT 1800)
endif()
set_tests_properties(legion_scale_one100 PROPERTIES
                     PASS_REGULAR_EXPRESSION "\"scale\\.arrive\\.n\":100,.*\"scale\\.stuck_end\":0,"
                     FAIL_REGULAR_EXPRESSION "MISMATCH|serial != workers|legion_scenario:")
# Legion 10,000 stage 2 (exact optimisations): the brute-force references under TAK_LEGION_VERIFY (debug builds;
# compiled out of Release, where these only run the file). Each file is one a deliberate mutation trips:
# battle-assault the join index (registerMove picks the first joinable group of all), crosslong awareScan's
# broad phase (a dropped mover with a conflict), gap8 gateCommit's static-decline memo (docs/legion-10000-progress.md).
foreach(legion_scn battle-assault crosslong gap8)
  add_test(NAME legion_verify_${legion_scn}
           COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/${legion_scn}.scn --mode legion --offsets 0 --workers)
  set_tests_properties(legion_verify_${legion_scn} PROPERTIES LABELS legion TIMEOUT 600
                       ENVIRONMENT "TAK_LEGION_VERIFY=1;TAK_SIM_WORKERS=3")
endforeach()
# Asset-free scenario set, part A (tools/scenarios/*.scn): both modes, serial and
# workers (the runner fails on a state or key mismatch), three start offsets.
# No bounds yet: a later task adds `--check` against baseline.json. The long
# ones (more than ~10 s of the optimized Debug build, one core) are nightly.
foreach(legion_scn corner-1x48 uturn gap6 sbend opentangent wall-1x48
                   doorform bridgeform mazeform)
  add_test(NAME legion_scenario_${legion_scn}
           COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/${legion_scn}.scn --mode both)
  set_tests_properties(legion_scenario_${legion_scn} PROPERTIES LABELS legion)
endforeach()
foreach(legion_scn corner-4x50 corner-8x56 corner-1x448 split-450-open split-450-pocket wall-4x50
                   gapsweep slowturn)
  add_test(NAME legion_scenario_${legion_scn}
           COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/${legion_scn}.scn --mode both)
  set_tests_properties(legion_scenario_${legion_scn} PROPERTIES LABELS "legion;nightly" TIMEOUT 1200)
endforeach()
# Asset-free scenario set, part B: awareness, flyers in streams and mixed
# formations, boats (island, strait), refresh churn. Same run as part A: both
# modes, serial and workers (a mismatch fails), three start offsets; no bounds
# yet. mixed2 and the strait fleets of 24+ big boats are KNOWN-FAILING behaviour
# (a report, not an assert): each file's header says which.
foreach(legion_scn aware-cross aware-headon aware-seen aware-unseen aware-attack
                   aware-adjacent liftstream island-1x48 strait mixed2 strait-4x24 strait-3x48 strait-2x48
                   refreshchurn refreshchurn-control)
  add_test(NAME legion_scenario_${legion_scn}
           COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/${legion_scn}.scn --mode both)
  set_tests_properties(legion_scenario_${legion_scn} PROPERTIES LABELS legion)
endforeach()
# W7 step 0 scenario fixtures: the route-behind metric (`behind`) and the aware.* counters (`probe aware`).
foreach(legion_scn aware-cross-behind crosslong)
  add_test(NAME legion_scenario_${legion_scn}
           COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/${legion_scn}.scn --mode both)
  set_tests_properties(legion_scenario_${legion_scn} PROPERTIES LABELS "legion;w7")
endforeach()
# W8 step 0 fixtures (PLAN 4 W8 step 0, docs/legion-exit-tables.md "W8 step 0"): the flowing-route guard doubleturn, the
# audit's gap sweep one gap per world (gap 6 is gap6), the MV-12 mixed-speed spacing probe at 0.7x / 0.5x / 0.4x, and the
# 8x30 multi-group corner. Same run as part A (both modes, serial and workers); the lane probes (`probe lanes`,
# lane / hug shapes in sbend) report with them. No bounds yet: S1's gates come with W8's steps.
foreach(legion_scn doubleturn gap8 gap10 gap14 gap20 mixed-spacing-07x mixed-spacing-05x mixed-spacing-04x)
  add_test(NAME legion_scenario_${legion_scn}
           COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/${legion_scn}.scn --mode both)
  set_tests_properties(legion_scenario_${legion_scn} PROPERTIES LABELS "legion;w8")
endforeach()
add_test(NAME legion_scenario_corner-8x30
         COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/corner-8x30.scn --mode both)
set_tests_properties(legion_scenario_corner-8x30 PROPERTIES LABELS "legion;w8;nightly" TIMEOUT 1200)
foreach(legion_scn densehead island-1x150 strait-3x80 strait-2x150)
  add_test(NAME legion_scenario_${legion_scn}
           COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/${legion_scn}.scn --mode both)
  set_tests_properties(legion_scenario_${legion_scn} PROPERTIES LABELS "legion;nightly" TIMEOUT 1200)
endforeach()
# W2 step 0 fixtures: AR-03 structure/Keep in an Alt+N squad, AR-11 unreachable orders and the
# ~8200-tick maze approach walk (`probe approach` keys). Run in both modes; the second form
# also checks every key against baseline.json over the gate's eleven offsets (tools/legion_check.py:
# small-count keys on all eleven, the rest on the core five), so a later W2 step that makes one worse
# fails here and one that improves it prints RATCHET (nightly: past the per-commit CI budget).
foreach(legion_scn structsquad structsquad-keep unreach-200 mazeapproach)
  add_test(NAME legion_scenario_${legion_scn}
           COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/${legion_scn}.scn --mode both)
  set_tests_properties(legion_scenario_${legion_scn} PROPERTIES LABELS "legion;w2" TIMEOUT 600)
  if(NOT WIN32)
    add_test(NAME legion_check_${legion_scn}
             COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/${legion_scn}.scn --mode both
                     --check ${CMAKE_SOURCE_DIR}/tools/scenarios/baseline.json)
    set_tests_properties(legion_check_${legion_scn} PROPERTIES LABELS "legion;w2;nightly" TIMEOUT 900)
  endif()
endforeach()
# W3 step 0 fixtures (PLAN 4 W3 step 0): the pocket, deadend, tail and doorplug probes of legion_world_test as .scn files,
# so the baseline gate (tools/legion_check.py) covers them over the eleven gate offsets and both modes. They report the
# W3 targets rather than meet them (pocket t90, deadend settled == n, tail open_still900, doorplug B crossings), so
# the baseline holds today's values and a W3 step that makes one worse fails here. Nightly: 2-60 s each per offset.
foreach(legion_scn
    deadend-w2-n120-closed-plain deadend-w2-n120-closed-squad deadend-w2-n120-room-plain
    deadend-w2-n120-room-squad deadend-w2-n40-closed-plain deadend-w2-n40-closed-squad
    deadend-w2-n40-room-plain deadend-w2-n40-room-squad deadend-w4-n120-closed-plain
    deadend-w4-n120-closed-squad deadend-w4-n120-room-plain deadend-w4-n120-room-squad
    deadend-w4-n40-closed-plain deadend-w4-n40-closed-squad deadend-w4-n40-room-plain
    deadend-w4-n40-room-squad deadend-w6-n120-closed-plain deadend-w6-n120-closed-squad
    deadend-w6-n120-room-plain deadend-w6-n120-room-squad deadend-w6-n40-closed-plain
    deadend-w6-n40-closed-squad deadend-w6-n40-room-plain deadend-w6-n40-room-squad doorplug-112 doorplug-124
    pocket-304-open pocket-304-wall pocket-615-open pocket-615-wall tail-corner380 tail-open304 tail-open570
    tail-wave)
  add_test(NAME legion_scenario_${legion_scn}
           COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/${legion_scn}.scn --mode both)
  set_tests_properties(legion_scenario_${legion_scn} PROPERTIES LABELS "legion;w3;nightly" TIMEOUT 1800)
  if(NOT WIN32)
    add_test(NAME legion_check_${legion_scn}
             COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/${legion_scn}.scn --mode both
                     --check ${CMAKE_SOURCE_DIR}/tools/scenarios/baseline.json)
    set_tests_properties(legion_check_${legion_scn} PROPERTIES LABELS "legion;w3;nightly" TIMEOUT 1800)
  endif()
endforeach()
# W5 step 0 fixtures (PLAN 4 W5 step 0; tools/scenarios/gen_w5_fixtures.py writes them): the AR-06 attackring rows
# (`reach` shape: attackers that ever get a shot, damage, the longest Holding run out of reach), fight-retarget and
# ringcross (`probe claims`: the arrival-slot claims invariant), the MV-05 lateblock modes, the AR-07 chase / goalblock
# re-seed fixtures, MV-18 keelturn and MV-07 staticblock (`creep`: the goal-area creep counter). Each reports the open
# problem it measures, so the baseline holds today's values and a W5 step that makes one worse fails
# legion_check_*; the gate (the PLAN target) is the exit of the step that fixes it. Sweep members of lateblock (the
# size sweep) are not baselined.
foreach(legion_scn
    attackring-f1-64 attackring-f2-64 attackring-squad-64 attackring-f1-16 attackring-r200-64 attackring-mob-40
    attackring-mob-100 attackring-struct-60 attackring-fight-60 attackring-guard-64 attackring-wall-64
    attackring-river-64 fight-retarget ringcross chase-1 chase-50 chase-line goalblock-200 goalblock-fight-200
    goalblock-near-200 keelturn-180 keelturn-90 keelturn-0 staticblock-own staticblock-other staticblock-open)
  add_test(NAME legion_scenario_${legion_scn}
           COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/${legion_scn}.scn --mode both)
  set_tests_properties(legion_scenario_${legion_scn} PROPERTIES LABELS "legion;w5" TIMEOUT 600)
  if(NOT WIN32)
    add_test(NAME legion_check_${legion_scn}
             COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/${legion_scn}.scn --mode both
                     --check ${CMAKE_SOURCE_DIR}/tools/scenarios/baseline.json)
    set_tests_properties(legion_check_${legion_scn} PROPERTIES LABELS "legion;w5;nightly" TIMEOUT 1800)
  endif()
endforeach()
foreach(legion_scn lateblock-none lateblock-before-16 lateblock-late-16)
  add_test(NAME legion_scenario_${legion_scn}
           COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/${legion_scn}.scn --mode both)
  set_tests_properties(legion_scenario_${legion_scn} PROPERTIES LABELS "legion;w5;nightly" TIMEOUT 1800)
  if(NOT WIN32)
    add_test(NAME legion_check_${legion_scn}
             COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/${legion_scn}.scn --mode both
                     --check ${CMAKE_SOURCE_DIR}/tools/scenarios/baseline.json)
    set_tests_properties(legion_check_${legion_scn} PROPERTIES LABELS "legion;w5;nightly" TIMEOUT 1800)
  endif()
endforeach()
# Long routes (2026-10-14 Legion review, item 6): serplong's serpentine puts the bodies past the old 16-bit
# field potentials (87,356 at its head), serplong-control is the same shape inside that range. Both walk the
# whole route (180,000 and 100,000 ticks), so they run nightly; legion_longroute is the per-commit check.
foreach(legion_scn serplong serplong-control)
  add_test(NAME legion_scenario_${legion_scn}
           COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/${legion_scn}.scn --mode both)
  set_tests_properties(legion_scenario_${legion_scn} PROPERTIES LABELS "legion;nightly" TIMEOUT 1800)
endforeach()
# One progress clock (2026-10-14 Legion review, item 1): a body that registered before its field reached it
# measured squared cells to its goal, then potential x64, so its stall clock aged through most of a free walk
# while its headway clock advanced (the observer's stall_stale_ticks, tools/legion_observe.h: 9,401 at offset 0
# on the parent, 8,248 to 9,664 over the gate offsets). Passes at two digits or fewer, serial equal to workers.
add_test(NAME legion_progressclock
         COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/battle-field-2x60.scn --mode legion --offsets 0 --json)
set_tests_properties(legion_progressclock PROPERTIES LABELS legion
                     PASS_REGULAR_EXPRESSION "\"serial_eq_workers\":true,.*\"stall_stale_ticks\":[0-9][0-9]?[,}]")
# Slots by arrival (2026-10-14 Legion review, item 4): 60 bodies sent into a room whose one door is on its far side.
# The formation ranks its members by the straight line to the click, which the way round the room turns over: on
# the parent the first bodies through the door kept the slots beside it and 22 to 37 settled outside, never done
# at any gate offset. Passes when, over the core five, the median run has every body in and the group done,
# serial equal to workers.
add_test(NAME legion_farroom
         COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/farroom.scn --mode legion --offsets 0,1,-1,2,-2 --json)
set_tests_properties(legion_farroom PROPERTIES LABELS legion
                     PASS_REGULAR_EXPRESSION "\"serial_eq_workers\":true,.*\"g\\.A\\.done\":[0-9]+,\"g\\.A\\.stops\":[0-9]+,\"g\\.A\\.left_behind\":0,")
# Passage lanes (2026-10-14 Legion review, item 3): 48 bodies take a 14-cell gap in a wall several abreast. On the
# parent the bodies that came through after the crowd had thinned walked straight for their slots, diagonally across
# the gap, through the lanes the earlier ones had held: 17 to 47 lane swaps in the gap strip over the core five
# (median 27). Passes when the median run swaps none there and the group is done, serial equal to workers.
add_test(NAME legion_passagelanes
         COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/gap14.scn --mode legion --offsets 0,1,-1,2,-2 --json)
set_tests_properties(legion_passagelanes PROPERTIES LABELS legion
                     PASS_REGULAR_EXPRESSION "\"serial_eq_workers\":true,.*\"g\\.A\\.done\":[0-9]+,.*\"gate\\.top\\.crossings\":0,")
# One slot hand-out budget a tick (2026-10-14 Legion review, item 5): four 120-body clicks to four dead ends in one tick.
# The formation slot searches had a budget per point, and none for a formation's first 32 slots or for a member
# without a slot: on the parent they walk 602,176 ring cells in one tick (B's second part searching against the
# formation its first 8 bodies took), every point's queue its own 131,072 on the ticks after. Passes when the
# per-tick peak stays under 150,000 (kHandOutCells, 131,072, plus the searches that start before it is spent),
# serial equal to workers.
add_test(NAME legion_handout
         COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/deadend-multi-4x120.scn --mode legion --offsets 0 --json)
set_tests_properties(legion_handout PROPERTIES LABELS legion
                     PASS_REGULAR_EXPRESSION "\"serial_eq_workers\":true,.*\"work\\.formation_ring_cells\\.max\":([0-9]?[0-9]?[0-9]?[0-9]?[0-9]|1[0-4][0-9][0-9][0-9][0-9]),")
# Ring upkeep (2026-10-19, ~100-body fixtures in tools/scenarios/legion100). Combat brakes an attacker the moment it
# is in reach, so a ring spot with a body firing on or beside it is spent; on the parent the field still led the army
# to the first arc it met (100 attackers against a walled target seen from one side: a jam on that side, the far
# arc empty). Passes when, at offsets 3,-3,4 (the target on the wall's corner, seen from part of the ring), the
# median damage dealt is 1400 or more (parent 1162, Retail 2307 / 2135 / 2227), serial equal to workers.
add_test(NAME legion_ringupkeep
         COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/legion100/attackring-wall-100.scn --mode legion --offsets 3,-3,4 --json)
set_tests_properties(legion_ringupkeep PROPERTIES LABELS legion
                     PASS_REGULAR_EXPRESSION "\"serial_eq_workers\":true,.*\"reach\\.ring\\.damage_end\":(1[4-9][0-9][0-9]|[2-9][0-9][0-9][0-9]|[0-9][0-9][0-9][0-9][0-9]+),")
# An attack on a target walled in out of the attackers' region (offset 0 of the same fixture) keeps its order and plans
# a ring in reach round the walls, as Retail's attackers press up to them; on the parent the army walked to the one
# nearest reachable point, piled against one face and dropped its orders there (19 attackers in reach at the end,
# Retail 61). Passes at 30 or more, serial equal to workers.
add_test(NAME legion_attackwalled
         COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/legion100/attackring-wall-100.scn --mode legion --offsets 0 --json)
set_tests_properties(legion_attackwalled PROPERTIES LABELS legion
                     PASS_REGULAR_EXPRESSION "\"serial_eq_workers\":true,.*\"reach\\.ring\\.now_end\":([3-9][0-9]|[1-9][0-9][0-9]),")
# A Move group crossing its own firing ring (ringcross-100: 50 attackers ring a target, 50 movers pass at tick 1500).
# On the parent the ring members held behind the firing line stood out of reach in the movers' way: over offsets
# 0,1,-1 in 12000 ticks the group's t90 is 5458 / never / 5551 and 38 attackers get in reach (Retail 3926, 43).
# Passes when the median t90 is under 5400 and at least 40 attackers get in reach, serial equal to workers.
add_test(NAME legion_ringcross
         COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/legion100/ringcross-100.scn --mode legion --offsets 0,1,-1 --ticks 12000 --json)
set_tests_properties(legion_ringcross PROPERTIES LABELS legion
                     PASS_REGULAR_EXPRESSION "\"serial_eq_workers\":true,.*\"g\\.M\\.t90\":([1-4][0-9][0-9][0-9]|5[0-3][0-9][0-9]),.*\"reach\\.ring\\.ever_end\":(4[0-9]|50),")
# Pressing in never shuffles (2026-10-19 review): a held attacker's step into reach must end in reach. On the first
# ring-upkeep head it tested reach at the neighbour origin's centre while the step ends half a cell short, so a body
# stepped in, was not braked, walked back out and pressed again (attackring-wall-64 offset -4: 12 of 14 presses from
# the same origin as the member's previous one). Passes with no such repeat at any gate offset.
add_test(NAME legion_pressin
         COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/attackring-wall-64.scn --mode legion --offsets gate --serial --json)
set_tests_properties(legion_pressin PROPERTIES LABELS legion
                     PASS_REGULAR_EXPRESSION "\"scenario\":\"attackring-wall-64\""
                     FAIL_REGULAR_EXPRESSION "\"work\\.reach_press_repeats\\.total\":(\\[|[1-9])")
# Legion work-scaling gate (W(4N0)/W(N0), slot work per slot, upkeep per group,
# every counter against tools/legion_cost_baseline.json) and the battle
# scenarios' window tables (tools/legion_cost.py). Counters only: wall time is
# printed, never gated. The larger battles run nightly.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME legion_cost
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_cost.py
                   --runner $<TARGET_FILE:legion_scenario> --tmpdir ${CMAKE_BINARY_DIR}/legion_cost_tmp)
  set_tests_properties(legion_cost PROPERTIES LABELS legion TIMEOUT 600)
  foreach(battle battle-field-2x60 battle-field-2x250 battle-field-2x500 battle-assault)
    add_test(NAME legion_scenario_${battle}
             COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_cost.py battle
                     ${CMAKE_SOURCE_DIR}/tools/scenarios/${battle}.scn
                     --runner $<TARGET_FILE:legion_scenario> --tmpdir ${CMAKE_BINARY_DIR}/legion_cost_tmp)
  endforeach()
  set_tests_properties(legion_scenario_battle-field-2x60 PROPERTIES LABELS legion TIMEOUT 600)
  set_tests_properties(legion_scenario_battle-field-2x250 legion_scenario_battle-field-2x500
                       legion_scenario_battle-assault PROPERTIES LABELS "legion;nightly" TIMEOUT 3600)
  # The committed W5 fixtures are the output of tools/scenarios/gen_w5_fixtures.py (edit the script, not the files).
  add_test(NAME legion_w5_fixtures
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/scenarios/gen_w5_fixtures.py --check)
  set_tests_properties(legion_w5_fixtures PROPERTIES LABELS "legion;w5" TIMEOUT 120)
  # The baseline gate (tools/legion_check.py): every baseline.json entry, exception and accepted
  # regression carries a reason, and the gate's synthetic unit tests.
  add_test(NAME legion_baseline_reasons
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_baseline_reasons_test.py
                   ${CMAKE_SOURCE_DIR}/tools/scenarios/baseline.json)
  set_tests_properties(legion_baseline_reasons PROPERTIES LABELS legion TIMEOUT 120)
  # docs/legion-pathfinding.md stays tied to the instruments: every Known weakness cites a fixture and key (or a
  # test) that exists, and the quoted field quota is kFieldQuota. The selftest renames one cited scenario, key and
  # test in a copy and fails unless each is caught (tools/legion_doc_links.py).
  add_test(NAME legion_doc_links
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_doc_links.py --root ${CMAKE_SOURCE_DIR})
  add_test(NAME legion_doc_links_selftest
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_doc_links.py --root ${CMAKE_SOURCE_DIR} --selftest)
  set_tests_properties(legion_doc_links legion_doc_links_selftest PROPERTIES LABELS legion TIMEOUT 120)
  # Legion 10,000 stage 4: the field scheduler under continuous load (tools/scenarios/scale/sched-load.scn:
  # field requests faster than the quota serves them for the whole run). Service gaps and start-request ages
  # within the policy's bounds, no body waiting over 60 ticks for a field, serial == workers
  # (tools/legion_sched_load_test.py). The _div run divides the registration, plane-build and awareness
  # budgets by 512 under TAK_LEGION_VERIFY: the registration FIFO (deferred registrations), the incremental
  # plane builds and the resumable awareness scan all run over budget, with every index checked.
  # legion_sched_mix (sched-mix.scn, a two-map field allowance): memory pressure -- the fit scan, the
  # reservation, request promotion and shares behind a stopped scan run in the sim, and every request's
  # wait stays within a bound derived from the allowance, the tenure and the measured queue.
  # (Stage 6: sched-load's squads take tile chains in the game, and then no longer keep the quota saturated; the
  # two load tests run it with every route the window build, --no-hier-routes, so they still test the scheduler
  # under the continuous load they were written for.)
  add_test(NAME legion_sched_load
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_sched_load_test.py
                   $<TARGET_FILE:legion_scenario> ${CMAKE_SOURCE_DIR}/tools/scenarios/scale/sched-load.scn --no-hier-routes)
  add_test(NAME legion_sched_load_div
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_sched_load_test.py
                   $<TARGET_FILE:legion_scenario> ${CMAKE_SOURCE_DIR}/tools/scenarios/scale/sched-load.scn --budget-div 512
                   --no-hier-routes)
  add_test(NAME legion_sched_mix
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_sched_load_test.py
                   $<TARGET_FILE:legion_scenario> ${CMAKE_SOURCE_DIR}/tools/scenarios/scale/sched-mix.scn)
  # legion_sched_pin: many-1000 under a two-map allowance, where fields shared by groups that never finish
  # once pinned the room a reservation waited for (admissions froze for the rest of the run): admission
  # liveness. legion_sched_inflight: many-2500, where refreshes promoted out of their class used to pile up
  # past the windows while memory was short (78 in flight): the per-kind cap.
  add_test(NAME legion_sched_pin
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_sched_load_test.py
                   $<TARGET_FILE:legion_scenario> ${CMAKE_SOURCE_DIR}/tools/scenarios/scale/many-1000.scn
                   --field-bytes 778752 --ticks 3000)
  add_test(NAME legion_sched_inflight
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_sched_load_test.py
                   $<TARGET_FILE:legion_scenario> ${CMAKE_SOURCE_DIR}/tools/scenarios/scale/many-2500.scn)
  set_tests_properties(legion_sched_load legion_sched_load_div legion_sched_mix legion_sched_pin legion_sched_inflight
                       PROPERTIES LABELS legion TIMEOUT 900)
  if(TAK_SLOW_TEST_BUILD)
    # Unoptimized: 613 s alone in Clang Debug, 862 s of its 900 s in a parallel Clang Debug suite (2026-10-18).
    set_tests_properties(legion_sched_inflight PROPERTIES TIMEOUT 1800)
  endif()
  set_tests_properties(legion_sched_load_div PROPERTIES ENVIRONMENT "TAK_LEGION_VERIFY=1;TAK_SIM_WORKERS=3")
  # Legion 10,000 stage 5: the route consumers that state their region (lanes, slots, rings, the formation
  # entrance, awareness) on paused routes, every first build pausable (--pause-all), under TAK_LEGION_VERIFY,
  # serial == workers; each consumer asked a paused build for its region and no read was uncertain
  # (tools/legion_route_paused_test.py).
  add_test(NAME legion_route_paused
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_route_paused_test.py
                   $<TARGET_FILE:legion_scenario> ${CMAKE_SOURCE_DIR}/tools/scenarios --data "${TAK_TEST_DATA}")
  set_tests_properties(legion_route_paused PROPERTIES LABELS legion TIMEOUT 900
                       ENVIRONMENT "TAK_LEGION_VERIFY=1;TAK_SIM_WORKERS=3")
  if(TAK_SLOW_TEST_BUILD)
    # Unoptimized: 1266 s alone in Clang Debug (2026-10-18); it timed out at 900 s in every unoptimized suite
    # of the 0.8.4 sweep (GCC -O0, Clang Debug, ASan). Optimized: 43 s Release, 197 s -O2 -g in a parallel suite.
    set_tests_properties(legion_route_paused PROPERTIES TIMEOUT 3600)
  endif()
  # Legion 10,000 stage 9: the parallel planning jobs (chain tiles built beside the serial pass, plane rows in
  # chunks) decide nothing: the state hash and every observer key equal the serial run at 1, 3 and 7 workers, with
  # the jobs' completion order perturbed, and forced inline under TAK_LEGION_VERIFY (tools/legion_parallel_test.py).
  add_test(NAME legion_parallel
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_parallel_test.py
                   $<TARGET_FILE:legion_scenario> ${CMAKE_SOURCE_DIR}/tools/scenarios/scale --data "${TAK_TEST_DATA}")
  set_tests_properties(legion_parallel PROPERTIES LABELS legion TIMEOUT 1800 SKIP_RETURN_CODE 77)
  if(TAK_SLOW_TEST_BUILD)
    # Unoptimized: 1605 s alone at GCC -O0 (8 cores, 2026-10-18); it timed out at 1800 s in every unoptimized
    # suite of the 0.8.4 sweep. Optimized: ~750 s in a parallel Release or -O2 -g suite.
    set_tests_properties(legion_parallel PROPERTIES TIMEOUT 5400)
  endif()
  # Legion 10,000 stage 7: crossing walkers no longer stand in the knots they form (scatter-400: 400 single
  # walkers to unrelated destinations, serial and workers; tools/legion_traffic_test.py).
  add_test(NAME legion_traffic
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_traffic_test.py
                   $<TARGET_FILE:legion_scenario> ${CMAKE_SOURCE_DIR}/tools/scenarios/scale --data "${TAK_TEST_DATA}")
  set_tests_properties(legion_traffic PROPERTIES LABELS legion TIMEOUT 900 SKIP_RETURN_CODE 77)
  # Legion 10,000 stage 8: a formation member gaining on its own slot beyond the click is not settled short of
  # it (one-1000: at least 960 of 1,000 arrive; tools/legion_arrival_test.py).
  add_test(NAME legion_arrival
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_arrival_test.py
                   $<TARGET_FILE:legion_scenario> ${CMAKE_SOURCE_DIR}/tools/scenarios/scale --data "${TAK_TEST_DATA}")
  set_tests_properties(legion_arrival PROPERTIES LABELS legion TIMEOUT 900 SKIP_RETURN_CODE 77)
  # Legion 10,000 stage 8: chokepoints as queues -- route choice by travel time plus queue delay and admission at
  # narrow passages (bridges-1000: both bridges used, at least 450 of 1,000 arrive; maze-1000: groups take the other
  # doors, at least 850 arrive; tools/legion_choke_test.py).
  add_test(NAME legion_choke
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_choke_test.py
                   $<TARGET_FILE:legion_scenario> ${CMAKE_SOURCE_DIR}/tools/scenarios/scale --data "${TAK_TEST_DATA}")
  set_tests_properties(legion_choke PROPERTIES LABELS legion TIMEOUT 1200 SKIP_RETURN_CODE 77)
  # Legion 10,000 stage 8: shared destination areas -- churn-1000's half stopped and sent again to the point the
  # other half is arriving at joins its area instead of planning round it as traffic (at least 780 of 1,000 arrive,
  # at most 40 stuck; the previous commit 717 / 73).
  add_test(NAME legion_sharedarea
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_scale_check.py
                   $<TARGET_FILE:legion_scenario> ${CMAKE_SOURCE_DIR}/tools/scenarios/scale churn-1000 --data "${TAK_TEST_DATA}"
                   --min scale.arrive.shared_areas=1 --min scale.arrive.n=780 --max scale.stuck_end=40)
  set_tests_properties(legion_sharedarea PROPERTIES LABELS legion TIMEOUT 900 SKIP_RETURN_CODE 77)
  # Legion 10,000 stage 8: a formation member with a free step toward its own slot is not pressed (one-1000: at
  # least 995 of 1,000 arrive, the 99th percentile too, at most 5 stuck; the previous commit 978 / never / 14).
  add_test(NAME legion_arrival_press
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_scale_check.py
                   $<TARGET_FILE:legion_scenario> ${CMAKE_SOURCE_DIR}/tools/scenarios/scale one-1000 --data "${TAK_TEST_DATA}"
                   --min scale.arrive.n=995 --min scale.arrive.t99=1 --max scale.stuck_end=5)
  set_tests_properties(legion_arrival_press PROPERTIES LABELS legion TIMEOUT 900 SKIP_RETURN_CODE 77)
  # Legion 10,000 stage 8: an entrance per group (room2door: one order of 2x2 and 3x3 bodies into a room through
  # two doors; the second group finds its own entrance on its own plane, and all 100 arrive).
  add_test(NAME legion_entrances
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_scale_check.py
                   $<TARGET_FILE:legion_scenario> ${CMAKE_SOURCE_DIR}/tools/scenarios/scale room2door --data "${TAK_TEST_DATA}"
                   --min scale.arrive.group_entrances=1 --min scale.arrive.n=100 --max scale.stuck_end=0)
  set_tests_properties(legion_entrances PROPERTIES LABELS legion TIMEOUT 600 SKIP_RETURN_CODE 77)
  # One passage, one admission (2026-10-19): two allied armies of 50 head on at bridges-100's 12-cell bridge. A group
  # re-planned onto a tile chain skipped admission (a chain records no labels) and walked into the strip the other
  # way held (offset -5: 36 arrive, 64 stuck), and an admission that lapsed when the token turned left its bodies
  # standing in the bridge's mouth, in the way of the stream let in next (offset 0, once chains were admitted: 52 /
  # 48). Checked at both offsets: at least 95 of 100 arrive and at most 2 stand stuck at the end.
  add_test(NAME legion_passage_admission
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_scale_check.py
                   $<TARGET_FILE:legion_scenario> ${CMAKE_SOURCE_DIR}/tools/scenarios/legion100 bridges-100 --data "${TAK_TEST_DATA}"
                   --offsets 0,-5 --min scale.arrive.n=95 --max scale.stuck_end=2)
  set_tests_properties(legion_passage_admission PROPERTIES LABELS legion TIMEOUT 600 SKIP_RETURN_CODE 77)
  # ... and on a bridge of varying width (taperbridge-100: 12-cell mouths, a 6-cell middle, five sector lines): the
  # passage is one admission over all its records, and a chain group's last few bodies (fewer than 8 left) still
  # ask before walking in. On the parent the stragglers met the other army in the middle (offset 0: 21 stuck,
  # -4: 25). Checked at 0 and -4: at most 2 stand stuck at the end.
  add_test(NAME legion_passage_taper
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_scale_check.py
                   $<TARGET_FILE:legion_scenario> ${CMAKE_SOURCE_DIR}/tools/scenarios taperbridge-100 --data "${TAK_TEST_DATA}"
                   --offsets 0,-4 --max scale.stuck_end=2)
  set_tests_properties(legion_passage_taper PROPERTIES LABELS legion TIMEOUT 600 SKIP_RETURN_CODE 77)
  # Head-on meets near both goals (many-100: two pairs of 25-body orders sent to each other's positions meet
  # within kPivotNear of both goals; the meet band now holds up to the destination area plus one lead): at least
  # 98 of 100 arrive, the 90th percentile within 1,000 ticks (the previous commit 96 / 1,616; Retail 92 / 1,144).
  add_test(NAME legion_meetnear
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_scale_check.py
                   $<TARGET_FILE:legion_scenario> ${CMAKE_SOURCE_DIR}/tools/scenarios/scale many-100 --data "${TAK_TEST_DATA}"
                   --min scale.arrive.n=98 --min scale.arrive.t90=1 --max scale.arrive.t90=1000 --max scale.stuck_end=0)
  set_tests_properties(legion_meetnear PROPERTIES LABELS legion TIMEOUT 600 SKIP_RETURN_CODE 77)
  # Departures through an arrived crowd (many-1000: each order's last bodies leave through the next order's
  # arrivals; settled friendly bodies of any allied player yield or part for them, and a long detour round a
  # crowd keeps travel speed): at least 900 of 1,000 arrive, at most 20 stuck (the previous commit 746 / 73).
  add_test(NAME legion_departure
           COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/tools/legion_scale_check.py
                   $<TARGET_FILE:legion_scenario> ${CMAKE_SOURCE_DIR}/tools/scenarios/scale many-1000 --data "${TAK_TEST_DATA}"
                   --min scale.arrive.n=900 --max scale.stuck_end=20)
  set_tests_properties(legion_departure PROPERTIES LABELS legion TIMEOUT 900 SKIP_RETURN_CODE 77)
endif()
# Legion navigation requirements as World checks (tools/legion_acceptance_test.cpp).
# Legion asserts; Retail runs the same checks with --report (comparison
# evidence, never failing).
add_executable(legion_acceptance_test tools/legion_acceptance_test.cpp)
target_link_libraries(legion_acceptance_test PRIVATE tak-formats)
foreach(legion_check singleunit group jagged trapped crowdheld)
  add_test(NAME legion_acceptance_${legion_check}_legion COMMAND legion_acceptance_test legion ${legion_check})
  set_tests_properties(legion_acceptance_${legion_check}_legion PROPERTIES TIMEOUT 600 LABELS legion)
  add_test(NAME legion_acceptance_${legion_check}_retail
           COMMAND legion_acceptance_test retail ${legion_check} --report)
  set_tests_properties(legion_acceptance_${legion_check}_retail PROPERTIES TIMEOUT 600 LABELS legion)
endforeach()
# crowdheld is a RATCHET, not a pass-all: 64 distinct goals packed at a 3-cell
# pitch (1-cell gaps) behind a gated door. The block leaves deepest-first, but
# the door reorders a pair in one row; the shallower unit settles first and
# seals the deeper goal, which is reachable only through neighbouring goals.
# Legion reaches 62/64 in goal at the W0 base (AR-09, docs/legion-pathfinding.md
# "Known weaknesses"); the test passes at 61 or more in goal (the physical_in_goal
# rule prints its value) and still fails on any spin, terrain-stuck unit or a
# lower count. Group right-clicks use a shared point with re-choosable slots and
# do not create this layout. Raise the 61 with the count as AR-09 improves.
# The one terrain-stuck unit (units_ever_terrain_stuck 1) is a limit the user accepted at the close of
# the Legion plan (decision W9-1, "Known weaknesses"), so only spin still fails the test.
set_tests_properties(legion_acceptance_crowdheld_legion PROPERTIES
                     PASS_REGULAR_EXPRESSION "physical_in_goal == moving \\(value 6[1-4]\\)"
                     FAIL_REGULAR_EXPRESSION "FAIL: spin_while")
add_executable(retail_search_scratch_test tools/retail_search_scratch_test.cpp)
target_include_directories(retail_search_scratch_test PRIVATE src)
add_executable(retail_group_test tools/retail_group_test.cpp)
target_link_libraries(retail_group_test PRIVATE tak-formats)
add_executable(cobanim_test tools/cobanim_test.cpp)
add_executable(unit_gift_test tools/unit_gift_test.cpp)
target_link_libraries(unit_gift_test PRIVATE tak-formats)
add_executable(cmdflow_test tools/cmdflow_test.cpp)
add_executable(conn_test tools/conn_test.cpp)
add_executable(validationworker_test tools/validationworker_test.cpp)
target_link_libraries(validationworker_test PRIVATE tak-formats)
add_test(NAME validationworker COMMAND validationworker_test)
set_tests_properties(validationworker PROPERTIES TIMEOUT 10)

add_executable(tdf_limits_test tools/tdf_limits_test.cpp)
target_link_libraries(tdf_limits_test PRIVATE tak-formats)
add_test(NAME tdf_limits COMMAND tdf_limits_test)
set_tests_properties(tdf_limits PROPERTIES TIMEOUT 10)

add_executable(server_commands_test tools/server_commands_test.cpp)
add_executable(server_address_test tools/server_address_test.cpp)
target_include_directories(server_address_test PRIVATE src)
add_test(NAME server_address COMMAND server_address_test)
add_executable(session_failure_test tools/session_failure_test.cpp)
target_include_directories(session_failure_test PRIVATE src)
add_test(NAME session_failure COMMAND session_failure_test)
add_executable(local_server_test tools/local_server_test.cpp)
target_include_directories(local_server_test PRIVATE src)
add_test(NAME local_server COMMAND local_server_test)
set_tests_properties(local_server PROPERTIES TIMEOUT 30)
target_link_libraries(server_commands_test PRIVATE tak-formats)
add_test(NAME server_commands COMMAND server_commands_test)
# Selection-order transactions at the server (server/orders.h, protocol 267).
add_executable(server_orders_test tools/server_orders_test.cpp)
target_link_libraries(server_orders_test PRIVATE tak-formats)
add_test(NAME server_orders COMMAND server_orders_test)
# Explicit selection orders end to end: codec, client packing, sim application (protocol 267).
add_executable(selection_order_test tools/selection_order_test.cpp)
target_link_libraries(selection_order_test PRIVATE tak-formats)
add_test(NAME selection_order COMMAND selection_order_test)
set_tests_properties(selection_order PROPERTIES LABELS legion)
add_executable(override_transfer_test tools/override_transfer_test.cpp)
target_link_libraries(override_transfer_test PRIVATE tak-formats)
add_executable(override_packs_test tools/override_packs_test.cpp)
target_link_libraries(override_packs_test PRIVATE tak-formats)
add_test(NAME override_packs COMMAND override_packs_test)
add_executable(room_worker_test tools/room_worker_test.cpp src/server/roomtick.cpp src/ai/ai.cpp)
target_link_libraries(room_worker_test PRIVATE tak-formats)
add_test(NAME room_worker COMMAND room_worker_test)
set_tests_properties(room_worker PROPERTIES TIMEOUT 30)
# Server tick pacing on a simulated clock: exact rates, speed changes, bounded catch-up.
add_executable(tick_pacer_test tools/tick_pacer_test.cpp)
target_include_directories(tick_pacer_test PRIVATE src)
add_test(NAME tick_pacer COMMAND tick_pacer_test)
add_executable(featgen_test tools/featgen_test.cpp)
add_executable(transport_test tools/transport_test.cpp)
add_executable(production_test tools/production_test.cpp)
add_executable(replay_test tools/replay_test.cpp)
add_executable(pathblock_test tools/pathblock_test.cpp)
add_executable(movement_orders_test tools/movement_orders_test.cpp)
target_link_libraries(movement_orders_test PRIVATE tak-formats)
add_executable(retail_motion_test tools/retail_motion_test.cpp)
add_executable(retail_script_test tools/retail_script_test.cpp)
target_link_libraries(retail_script_test PRIVATE tak-formats)
add_test(NAME retail_script COMMAND retail_script_test)
add_executable(vfs_names_test tools/vfs_names_test.cpp)
target_link_libraries(vfs_names_test PRIVATE tak-formats)
add_test(NAME vfs_names COMMAND vfs_names_test)
add_library(tak-gpustats STATIC src/util/nvml.cpp src/util/windowsgpu.cpp)
target_include_directories(tak-gpustats PUBLIC src)
target_link_libraries(tak-gpustats PRIVATE ${CMAKE_DL_LIBS})
if(WIN32)
  target_link_libraries(tak-gpustats PRIVATE pdh dxgi)
endif()
add_executable(nvml_test tools/nvml_test.cpp)
target_link_libraries(nvml_test PRIVATE tak-gpustats)
add_test(NAME nvml COMMAND nvml_test)
add_executable(windowsgpu_test tools/windowsgpu_test.cpp)
target_link_libraries(windowsgpu_test PRIVATE tak-gpustats)
add_test(NAME windowsgpu COMMAND windowsgpu_test)
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
add_executable(map_transfer_test tools/map_transfer_test.cpp)
target_link_libraries(map_transfer_test PRIVATE tak-formats)
add_test(NAME map_transfer COMMAND map_transfer_test)
add_executable(scenario_test tools/scenario_test.cpp)
target_link_libraries(scenario_test PRIVATE tak-formats)
add_test(NAME scenario COMMAND scenario_test)
add_executable(scenario_restrictions_test tools/scenario_restrictions_test.cpp)
target_link_libraries(scenario_restrictions_test PRIVATE tak-formats)
add_test(NAME scenario_restrictions COMMAND scenario_restrictions_test)
# MAP SCENARIO (protocol 259): the map scenario inspector, and its agreement with setupMatch.
add_executable(scenario_info_test tools/scenario_info_test.cpp)
target_link_libraries(scenario_info_test PRIVATE tak-formats)
add_test(NAME scenario_info COMMAND scenario_info_test)
add_executable(scenario_outcome_test tools/scenario_outcome_test.cpp)
target_link_libraries(scenario_outcome_test PRIVATE tak-formats)
add_test(NAME scenario_outcome COMMAND scenario_outcome_test)
add_executable(scenario_stats_test tools/scenario_stats_test.cpp)
target_link_libraries(scenario_stats_test PRIVATE tak-formats)
add_test(NAME scenario_stats COMMAND scenario_stats_test)
add_executable(unit_retirement_test tools/unit_retirement_test.cpp)
target_link_libraries(unit_retirement_test PRIVATE tak-formats)
add_test(NAME unit_retirement COMMAND unit_retirement_test)
add_executable(scenario_resources_test tools/scenario_resources_test.cpp)
target_link_libraries(scenario_resources_test PRIVATE tak-formats)
add_test(NAME scenario_resources COMMAND scenario_resources_test)
add_executable(scenario_actions_test tools/scenario_actions_test.cpp)
target_link_libraries(scenario_actions_test PRIVATE tak-formats)
add_test(NAME scenario_actions COMMAND scenario_actions_test)
add_executable(scenario_selectors_test tools/scenario_selectors_test.cpp)
target_link_libraries(scenario_selectors_test PRIVATE tak-formats)
add_test(NAME scenario_selectors COMMAND scenario_selectors_test)
add_executable(scenario_placement_test tools/scenario_placement_test.cpp src/cartographer/units.cpp)
target_link_libraries(scenario_placement_test PRIVATE tak-formats)
add_test(NAME scenario_placement COMMAND scenario_placement_test)

add_executable(mapgen_test tools/mapgen_test.cpp)
target_link_libraries(mapgen_test PRIVATE tak-formats)
add_test(NAME mapgen COMMAND mapgen_test)

add_executable(retail_visual_test tools/retail_visual_test.cpp)
target_link_libraries(retail_visual_test PRIVATE tak-formats)
add_test(NAME retail_visual COMMAND retail_visual_test)
add_executable(order_trail_test tools/order_trail_test.cpp src/client/ordertrail.cpp)
target_link_libraries(order_trail_test PRIVATE tak-formats)
add_test(NAME order_trail COMMAND order_trail_test)
add_executable(reclaim_pick_test tools/reclaim_pick_test.cpp)
target_link_libraries(reclaim_pick_test PRIVATE tak-formats)
add_test(NAME reclaim_pick COMMAND reclaim_pick_test)
add_executable(navigation_checkpoint_test tools/navigation_checkpoint_test.cpp)
target_link_libraries(navigation_checkpoint_test PRIVATE tak-formats)
# Legion mission goals (attack, chase, guard, fight-move, patrol, group patrol,
# repair and build approaches).
add_test(NAME legion_movement_orders COMMAND movement_orders_test --legion)
# Legion work/logistics goals: production exits and rallies, boarding, unload
# approaches and PARK rings (each asserts Legion served the leg).
add_test(NAME legion_production COMMAND production_test --legion)
add_test(NAME legion_transport COMMAND transport_test --legion)
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
add_executable(simpool_test tools/simpool_test.cpp)
target_link_libraries(simpool_test PRIVATE tak-formats)
add_executable(fixed_test tools/fixed_test.cpp)
add_executable(statsfit_test tools/statsfit_test.cpp)
add_executable(settings_test tools/settings_test.cpp)
add_executable(start_markers_test tools/start_markers_test.cpp)
add_executable(texture_filter_test tools/texture_filter_test.cpp)
add_executable(gpuvram_test tools/gpuvram_test.cpp)
    add_executable(footprobe tools/footprobe.cpp)
target_link_libraries(retailgap_test PRIVATE tak-formats)
target_link_libraries(conjure_test PRIVATE tak-formats)
target_link_libraries(turn_test PRIVATE tak-formats)
target_link_libraries(stuckrepro PRIVATE tak-formats)
target_link_libraries(placement_test PRIVATE tak-formats)
target_link_libraries(navblock_test PRIVATE tak-formats)
target_link_libraries(reclaimarea_test PRIVATE tak-formats)
target_link_libraries(campaign_test PRIVATE tak-formats)
target_link_libraries(campaign_conditions_test PRIVATE tak-formats)
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
target_link_libraries(start_markers_test PRIVATE tak-formats)
target_link_libraries(texture_filter_test PRIVATE tak-formats)
target_link_libraries(gpuvram_test PRIVATE tak-formats)

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
add_test(NAME fp_contract COMMAND fp_contract_test)
add_test(NAME sim_driver_equiv COMMAND sim_driver_equiv_test)
add_test(NAME pathsearch COMMAND pathsearch_test)
add_test(NAME retail_scheduler COMMAND retail_scheduler_test)
add_test(NAME retail_trace COMMAND retail_trace_test --selftest)
add_test(NAME retail_search_scratch COMMAND retail_search_scratch_test)
add_test(NAME retail_group COMMAND retail_group_test)
add_test(NAME auth COMMAND authtest)
add_test(NAME unit_gift COMMAND unit_gift_test)
add_test(NAME cmdflow COMMAND cmdflow_test)
add_test(NAME reclaimarea COMMAND reclaimarea_test)
add_test(NAME campaign COMMAND campaign_test)
add_test(NAME campaign_conditions COMMAND campaign_conditions_test)
add_test(NAME conn COMMAND conn_test)
add_test(NAME featgen COMMAND featgen_test)
add_test(NAME transport COMMAND transport_test)
add_test(NAME explode_capture COMMAND cobanim_test --explode-capture)
add_test(NAME production COMMAND production_test)
add_test(NAME replay COMMAND replay_test)
add_test(NAME pathblock COMMAND pathblock_test)
add_test(NAME movement_orders COMMAND movement_orders_test)
set_tests_properties(movement_orders PROPERTIES TIMEOUT 180)
add_test(NAME cadence COMMAND cadence_test)
add_test(NAME duration COMMAND duration_test)
add_test(NAME navincr COMMAND navincr_test)
add_test(NAME fixed COMMAND fixed_test)
add_test(NAME simpool COMMAND simpool_test)
add_test(NAME settings COMMAND settings_test)
add_test(NAME start_markers COMMAND start_markers_test)
add_test(NAME texture_filter COMMAND texture_filter_test)
add_test(NAME gpuvram COMMAND gpuvram_test)
add_test(NAME door_animation COMMAND door_animation_test)
add_test(NAME pathgeom COMMAND pathgeom_test)
add_test(NAME ambush COMMAND ambush_test)
add_executable(exploration_parallel_test tools/exploration_parallel_test.cpp)
target_link_libraries(exploration_parallel_test PRIVATE tak-formats)
add_test(NAME exploration_parallel COMMAND exploration_parallel_test)
add_executable(simpool_passes_test tools/simpool_passes_test.cpp)
target_link_libraries(simpool_passes_test PRIVATE tak-formats)
add_test(NAME simpool_passes COMMAND simpool_passes_test)
add_executable(vision_rules_test tools/vision_rules_test.cpp)
target_link_libraries(vision_rules_test PRIVATE tak-formats)
add_test(NAME vision_rules COMMAND vision_rules_test)
add_test(NAME statsfit COMMAND statsfit_test)
add_executable(systemcpu_test tools/systemcpu_test.cpp)
target_include_directories(systemcpu_test PRIVATE src)
add_test(NAME systemcpu COMMAND systemcpu_test)
add_executable(gaitpace_test tools/gaitpace_test.cpp)
target_link_libraries(gaitpace_test PRIVATE tak-formats)
add_test(NAME gaitpace COMMAND gaitpace_test)
add_executable(animation_roster_test tools/animation_roster_test.cpp)
target_link_libraries(animation_roster_test PRIVATE tak-formats)
add_executable(structure_combat_test tools/structure_combat_test.cpp)
target_link_libraries(structure_combat_test PRIVATE tak-formats)
add_executable(naval_combat_test tools/naval_combat_test.cpp)
target_link_libraries(naval_combat_test PRIVATE tak-formats)
add_executable(production_exit_test tools/production_exit_test.cpp)
target_link_libraries(production_exit_test PRIVATE tak-formats)
add_executable(flyer_combat_test tools/flyer_combat_test.cpp)
target_link_libraries(flyer_combat_test PRIVATE tak-formats)
add_executable(capture_combat_test tools/capture_combat_test.cpp)
target_link_libraries(capture_combat_test PRIVATE tak-formats)
add_test(NAME capture_combat COMMAND capture_combat_test)
set(TAK_TEST_DATA "" CACHE PATH "Retail install for the data-backed tests (optional)")
if(TAK_TEST_DATA)
    add_test(NAME builder_automation_data COMMAND builder_automation_test "${TAK_TEST_DATA}")
    set_tests_properties(builder_automation_data PROPERTIES TIMEOUT 300)
    add_test(NAME movement_orders_maze COMMAND movement_orders_test "${TAK_TEST_DATA}")
    set_tests_properties(movement_orders_maze PROPERTIES TIMEOUT 300)
    add_test(NAME movement_orders_trolls COMMAND movement_orders_test --trolls "${TAK_TEST_DATA}")
    # 600 s, like legion_movement_orders_maze: unoptimized it takes 238 s alone (Clang Debug,
    # 2026-10-15) but timed out at 300 s under a parallel -O0 suite; at -O2 it takes ~40 s.
    set_tests_properties(movement_orders_trolls PROPERTIES TIMEOUT 600)
    add_test(NAME legion_movement_orders_maze COMMAND movement_orders_test --legion "${TAK_TEST_DATA}")
    set_tests_properties(legion_movement_orders_maze PROPERTIES TIMEOUT 600)
    # Open-ground cohorts, and (separately, serial == workers) the 100-Troll
    # rally cohort on the generated maze.
    add_test(NAME legion_movement_orders_trolls COMMAND movement_orders_test --legion --trolls "${TAK_TEST_DATA}" open)
    add_test(NAME legion_movement_orders_trolls_maze COMMAND movement_orders_test --legion --trolls "${TAK_TEST_DATA}" maze)
    set_tests_properties(legion_movement_orders_trolls legion_movement_orders_trolls_maze PROPERTIES TIMEOUT 900)
    add_test(NAME map_transfer_data COMMAND map_transfer_test "${TAK_TEST_DATA}")
    add_test(NAME mapgen_roster COMMAND mapgen_test "${TAK_TEST_DATA}")
    add_test(NAME issue_selection_test_data COMMAND issue_selection_test "${TAK_TEST_DATA}")
    add_test(NAME mapgen_mana COMMAND mapgen_test "${TAK_TEST_DATA}" --mana)
    set_tests_properties(mapgen mapgen_mana PROPERTIES TIMEOUT 180)
    add_test(NAME mapgen_naval COMMAND mapgen_test "${TAK_TEST_DATA}" --naval)
    add_test(NAME mapgen_themes COMMAND mapgen_test "${TAK_TEST_DATA}" --themes)
    add_test(NAME mapgen_ports COMMAND mapgen_test "${TAK_TEST_DATA}" --ports)
    add_test(NAME campaign_roster COMMAND campaign_test "${TAK_TEST_DATA}")
    add_test(NAME structure_combat COMMAND structure_combat_test "${TAK_TEST_DATA}")
    add_test(NAME naval_combat COMMAND naval_combat_test "${TAK_TEST_DATA}")
    add_test(NAME legion_naval_combat COMMAND naval_combat_test "${TAK_TEST_DATA}" --legion)
    add_test(NAME transport_roster COMMAND transport_test "${TAK_TEST_DATA}")
    add_test(NAME transport_post_unload COMMAND transport_test --post-unload "${TAK_TEST_DATA}")
    add_test(NAME transport_map_roundtrip COMMAND transport_test
        --surface-transport-map-roundtrip-type "${TAK_TEST_DATA}"
        "Lake Lokken" vertrans araarch 240 120 240 350)
    add_test(NAME transport_map_roundtrip_crusades COMMAND transport_test
        --surface-transport-map-roundtrip-type "${TAK_TEST_DATA}"
        "Lake Lokken" vertrans araarch 240 120 240 350 1)
    add_test(NAME animation_roster COMMAND animation_roster_test "${TAK_TEST_DATA}")
    add_test(NAME production_exit COMMAND production_exit_test "${TAK_TEST_DATA}")
    add_test(NAME flyer_combat COMMAND flyer_combat_test "${TAK_TEST_DATA}")
    add_test(NAME capture_combat_roster COMMAND capture_combat_test "${TAK_TEST_DATA}")
    add_test(NAME gate_scripts COMMAND retail_script_test --gates "${TAK_TEST_DATA}")
    add_test(NAME scenery_damage COMMAND retail_script_test --features "${TAK_TEST_DATA}")
    add_test(NAME cobanim COMMAND cobanim_test "${CMAKE_SOURCE_DIR}/assets/extracted/all/scripts")
    add_test(NAME gaitpace_shipped COMMAND gaitpace_test "${CMAKE_SOURCE_DIR}/assets/extracted/all/scripts")
    set_tests_properties(animation_roster cobanim explode_capture PROPERTIES
        ENVIRONMENT "TAK_VERIFY_NATIVE_EXPORT=1")
    add_test(NAME retailgap COMMAND retailgap_test "${TAK_TEST_DATA}")
    add_test(NAME conjure COMMAND conjure_test "${TAK_TEST_DATA}")
    add_test(NAME death_lifecycle COMMAND death_lifecycle_test "${TAK_TEST_DATA}")
    add_test(NAME building_death COMMAND building_death_test "${TAK_TEST_DATA}")
    add_test(NAME clear_build COMMAND clear_build_test "${TAK_TEST_DATA}")
    add_test(NAME lodestone_upgrade COMMAND lodestone_upgrade_test "${TAK_TEST_DATA}")
    add_test(NAME turn COMMAND turn_test "${TAK_TEST_DATA}")
    add_test(NAME navblock COMMAND navblock_test "${TAK_TEST_DATA}")
    add_test(NAME placement COMMAND placement_test "${TAK_TEST_DATA}")
    # The Release client intentionally strips the local game/dev command path.
    # Keep this headless client integration fixture in Debug configurations.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_test(NAME diplomacy_ui COMMAND takclient game "Ulasem Arena"
            --data "${TAK_TEST_DATA}" --testbuild --time 0
            --shot "${CMAKE_CURRENT_BINARY_DIR}/diplomacy-test.png")
        set_tests_properties(diplomacy_ui PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;TAK_DIPLOMACY_TEST=1"
            PASS_REGULAR_EXPRESSION "PASS: diplomacy hotkey, roster, defaults, checkboxes, voice column, volume sliders"
            TIMEOUT 60)
        add_test(NAME retirement_ui COMMAND takclient game "Ulasem Arena"
            --data "${TAK_TEST_DATA}" --testbuild --time 0
            --shot "${CMAKE_CURRENT_BINARY_DIR}/retirement-ui.png")
        set_tests_properties(retirement_ui PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;TAK_DIPLOMACY_TEST=1;TAK_RETIREMENT_TEST=1"
            PASS_REGULAR_EXPRESSION "PASS: diplomacy hotkey, roster, defaults, checkboxes, voice column, volume sliders"
            TIMEOUT 60)
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
        # Killed/Dying sounds as the simulation chose them, through the client's
        # sound drain: exactly per death, never doubled, fog/class gated, at the
        # authored cry rates and variants for every Creon caller.
        add_test(NAME creon_death_sound COMMAND takclient game "Ulasem Arena"
            --data "${TAK_TEST_DATA}" --firetest --time 0.1
            --shot "${CMAKE_CURRENT_BINARY_DIR}/creon-death-sound-test.png")
        set_tests_properties(creon_death_sound PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;TAK_CREON_DEATH_SOUND_TEST=1;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/creon-death-sound-profile;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/creon-death-sound-profile;TAK_SETTINGS_FILE=${CMAKE_CURRENT_BINARY_DIR}/creon-death-sound-profile/settings.ini"
            PASS_REGULAR_EXPRESSION "PASS: Creon death sounds are the simulation's"
            TIMEOUT 120)
        # The same game again beside a peer that presents every batch late (a
        # rejoin or spectator catch-up): both must hear the same death sounds.
        add_test(NAME creon_death_sound_peers COMMAND ${CMAKE_COMMAND} "-DCLIENT=$<TARGET_FILE:takclient>"
            "-DDATA=${TAK_TEST_DATA}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/creon-death-sound-peers"
            -P "${CMAKE_SOURCE_DIR}/tools/creon_death_sound.cmake")
        set_tests_properties(creon_death_sound_peers PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/creon-death-sound-peers-profile;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/creon-death-sound-peers-profile;TAK_SETTINGS_FILE=${CMAKE_CURRENT_BINARY_DIR}/creon-death-sound-peers-profile/settings.ini"
            PASS_REGULAR_EXPRESSION "PASS: both peers hear the simulation's Creon death sounds"
            TIMEOUT 240)
        add_test(NAME weapon_impact_effect_route COMMAND takclient game "Lake Lokken"
            --data "${TAK_TEST_DATA}" --firetest --nofog --time 0.1
            --shot "${CMAKE_CURRENT_BINARY_DIR}/weapon-impact-effect-test.png")
        set_tests_properties(weapon_impact_effect_route PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;TAK_WEAPON_IMPACT_EFFECT_TEST=1"
            PASS_REGULAR_EXPRESSION "PASS: Arapult land/water authored impact variants"
            TIMEOUT 120)
    endif()
endif()
# Data-gated Legion scenarios (need the install; label data;nightly). The motion
# cases are tools/legion_group_motion's open/wall/cross with 12 real unit types,
# wanderers off (motion-wall-wanderers-on reproduces the old tool's confound);
# the gen1 routes are the seven big army orders of the legion-r9 replay on the
# user's generated map.
if(TAK_TEST_DATA)
    foreach(legion_motion open wall cross)
        add_test(NAME legion_motion_${legion_motion}
                 COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/motion-${legion_motion}.scn
                         --data "${TAK_TEST_DATA}" --json)
        set_tests_properties(legion_motion_${legion_motion} PROPERTIES
                             SKIP_RETURN_CODE 77 LABELS "data;nightly" TIMEOUT 600)
    endforeach()
    add_test(NAME legion_motion_wall_wanderers_on
             COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/motion-wall.scn
                     --data "${TAK_TEST_DATA}" --wanderers on --json)
    set_tests_properties(legion_motion_wall_wanderers_on PROPERTIES
                         SKIP_RETURN_CODE 77 LABELS "data;nightly" TIMEOUT 600)
    # Harvested situations (tools/scenarios/situations/README.txt): not faithful enough to gate on, so these only
    # check that each file loads and runs deterministically, serial == workers, in both modes.
    # Each of these is one of the longest tests in the suite (50-150 s), so it is split into independent
    # parts that run in parallel: legion_scenario checks serial == workers per (mode, offset) and gates
    # nothing across them, so the union of the parts is exactly the unsplit run.
    foreach(legion_situation r9-36060 r9-65850 r9-61930 r10-46292 r10-87332 r10-104540)
        foreach(legion_part legion retail)
            add_test(NAME legion_situation_${legion_situation}_${legion_part}
                     COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/situations/situation-${legion_situation}.scn
                             --data "${TAK_TEST_DATA}" --offsets 0 --mode ${legion_part} --json)
            set_tests_properties(legion_situation_${legion_situation}_${legion_part} PROPERTIES
                                 SKIP_RETURN_CODE 77 LABELS "data;nightly" TIMEOUT 1800)
        endforeach()
    endforeach()
    foreach(legion_route 21096 27459 36057 45941 54772 61923 65845)
        foreach(legion_part 0 1 -1)
            string(REPLACE "-" "m" legion_part_name "${legion_part}")
            add_test(NAME legion_gen1_route_${legion_route}_o${legion_part_name}
                     COMMAND legion_scenario ${CMAKE_SOURCE_DIR}/tools/scenarios/gen1-route-${legion_route}.scn
                             --data "${TAK_TEST_DATA}" --offsets ${legion_part} --json)
            set_tests_properties(legion_gen1_route_${legion_route}_o${legion_part_name} PROPERTIES
                                 SKIP_RETURN_CODE 77 LABELS "data;nightly" TIMEOUT 1800)
        endforeach()
    endforeach()
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
add_executable(naval_ai_test tools/naval_ai_test.cpp src/ai/ai.cpp)
target_link_libraries(naval_ai_test PRIVATE tak-formats)
if(TAK_TEST_DATA)
    add_test(NAME naval_ai COMMAND naval_ai_test "${TAK_TEST_DATA}")
endif()
add_executable(simperf tools/simperf.cpp src/ai/ai.cpp)
target_link_libraries(simperf PRIVATE tak-formats)

# --- Client (needs SDL2; skipped with a warning if unavailable) ---------------

option(TAK_BUILD_CLIENT "Build the SDL2 client (asset viewer + game)" ON)

# FFmpeg decodes the retail Bink (.bik) menu videos -- libavcodec carries a clean-room
# Bink decoder (LGPL). It is ALWAYS the minimal Bink and streaming static build from
# tools/build-ffmpeg-bink.sh; there is no dynamic link and no no-video build.
#
# The system libavcodec is never consulted, which is deliberate twice over: stock
# Fedora's libavcodec-free omits the Bink decoder entirely (so "it built" would not mean
# "videos play"), and its .pc files were the last place a shared libatomic could sneak
# back into an otherwise static binary. TAK_FFMPEG_PREFIX overrides where the static
# install lives.
set(TAK_FFMPEG_PREFIX "" CACHE PATH "Install prefix of the static Bink and streaming FFmpeg")

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  if(NOT TAK_FFMPEG_PREFIX)
    set(TAK_FFMPEG_PREFIX "${CMAKE_SOURCE_DIR}/third_party/ffmpeg-bink")
  endif()
  if(EXISTS "${TAK_FFMPEG_PREFIX}/lib/pkgconfig")
    set(ENV{PKG_CONFIG_PATH} "${TAK_FFMPEG_PREFIX}/lib/pkgconfig")
    # Order high-level -> low-level so the static archives resolve left-to-right.
    if(NOT EXISTS "${TAK_FFMPEG_PREFIX}/stream-build-version")
      message(FATAL_ERROR "Static FFmpeg needs streaming support; run tools/build-ffmpeg-bink.sh")
    endif()
    unset(__pkg_config_checked_FFMPEG CACHE)
    pkg_check_modules(FFMPEG libavformat libavcodec libswscale libswresample libavutil)
  else()
    message(FATAL_ERROR
      "The static Bink and streaming FFmpeg is missing: ${TAK_FFMPEG_PREFIX}\n"
      "  Build it once with:  ./tools/build-ffmpeg-bink.sh\n"
      "(FFmpeg is always linked statically; there is no dynamic build and no "
      "no-video build to fall back to.)")
  endif()
else()
  message(FATAL_ERROR "pkg-config is required to locate the static Bink and streaming FFmpeg")
endif()

# Multiplayer TLS shares the pinned, static OpenSSL distribution used by media
# on Linux. Other platforms build the same archives; no SSL DLLs are shipped.
foreach(_tls_lib ssl crypto)
  if(NOT EXISTS "${TAK_FFMPEG_PREFIX}/lib/lib${_tls_lib}.a")
    message(FATAL_ERROR "Missing static TLS archive; run tools/build-ffmpeg-bink.sh")
  endif()
endforeach()
add_library(tak_tls STATIC src/net/tls.cpp)
target_include_directories(tak_tls PUBLIC src PRIVATE "${TAK_FFMPEG_PREFIX}/include")
target_include_directories(tak-formats PRIVATE "${TAK_FFMPEG_PREFIX}/include")
target_link_libraries(tak_tls PUBLIC "${TAK_FFMPEG_PREFIX}/lib/libssl.a"
  "${TAK_FFMPEG_PREFIX}/lib/libcrypto.a" ${CMAKE_DL_LIBS} Threads::Threads)
if(WIN32)
  target_link_libraries(tak_tls PUBLIC crypt32 ws2_32 bcrypt advapi32 user32 gdi32)
elseif(APPLE)
  target_link_libraries(tak_tls PUBLIC "-framework Security" "-framework CoreFoundation")
endif()
target_link_libraries(tak-formats PUBLIC tak_tls)

add_executable(tls_test tools/tls_test.cpp)
target_include_directories(tls_test PRIVATE "${TAK_FFMPEG_PREFIX}/include")
target_link_libraries(tls_test PRIVATE tak-formats)
add_test(NAME tls COMMAND tls_test)

# One INTERFACE target both takclient and biktool link.
if(FFMPEG_FOUND)
  add_library(tak_ffmpeg INTERFACE)
  target_compile_definitions(tak_ffmpeg INTERFACE TAK_HAVE_FFMPEG)
  target_include_directories(tak_ffmpeg INTERFACE ${FFMPEG_STATIC_INCLUDE_DIRS})
  # Explicit archive paths: a stray .so/.dll import library in the prefix must
  # never silently turn a streaming dependency into a runtime requirement.
  set(_ff_archives "")
  foreach(_flag IN LISTS FFMPEG_STATIC_LDFLAGS)
    if(_flag MATCHES "^-l(avformat|avcodec|avutil|swscale|swresample|x264|ssl|crypto|va|va-drm|drm|vpl)$")
      set(_archive "${TAK_FFMPEG_PREFIX}/lib/lib${CMAKE_MATCH_1}.a")
      if(NOT EXISTS "${_archive}")
        message(FATAL_ERROR "Missing static streaming archive ${_archive}; run tools/build-ffmpeg-bink.sh")
      endif()
      list(APPEND _ff_archives "${_archive}")
    else()
      list(APPEND _ff_archives "${_flag}")
    endif()
  endforeach()
  set(FFMPEG_STATIC_LDFLAGS ${_ff_archives})
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
  message(STATUS "FFmpeg: STATIC Bink and streaming build from ${TAK_FFMPEG_PREFIX}")
endif()

add_executable(stream_network_test tools/stream_network_test.cpp src/video/stream.cpp)
target_compile_definitions(stream_network_test PRIVATE TAK_STREAM_TESTING)
target_link_libraries(stream_network_test PRIVATE tak-formats tak_ffmpeg)

add_executable(stream_test tools/stream_test.cpp src/video/stream.cpp)
target_compile_definitions(stream_test PRIVATE TAK_STREAM_TESTING=1)
target_link_libraries(stream_test PRIVATE tak-formats tak_ffmpeg)
add_test(NAME streaming COMMAND stream_test)
add_test(NAME streaming_overload COMMAND stream_test libx264 overload)
add_test(NAME streaming_native COMMAND stream_test libx264 native)
set_tests_properties(streaming PROPERTIES TIMEOUT 30)

# Streaming REMEMBER KEY: the stream key in the OS credential store (src/client/credstore.h),
# one platform file each. Linux dlopens libsecret at run time, so static release builds gain
# no runtime dependency; without it (or without a Secret Service) the option is greyed out.
if(WIN32)
  set(_tak_credstore src/client/credstore_win.cpp)
elseif(APPLE)
  set(_tak_credstore src/client/credstore_mac.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(_tak_credstore src/client/credstore_linux.cpp)
else()
  set(_tak_credstore src/client/credstore_none.cpp)
endif()
add_library(tak-credstore STATIC ${_tak_credstore} src/client/streamkey.cpp)
target_include_directories(tak-credstore PUBLIC src)
target_link_libraries(tak-credstore PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(tak-credstore PUBLIC advapi32)
elseif(APPLE)
  target_link_libraries(tak-credstore PUBLIC "-framework Security" "-framework CoreFoundation")
else()
  target_link_libraries(tak-credstore PUBLIC ${CMAKE_DL_LIBS})
endif()
# Round trip against the real store under a test-only name; exits 77 (skipped) when this
# session has no usable store (CI containers, ssh without a keychain, no Secret Service).
add_executable(credstore_test tools/credstore_test.cpp)
target_link_libraries(credstore_test PRIVATE tak-credstore)
add_test(NAME credstore COMMAND credstore_test)
set_tests_properties(credstore PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)

if(TAK_BUILD_CLIENT)
  find_package(SDL2 QUIET)
  if(SDL2_FOUND)
    add_executable(takclient src/client/crusadesscreen.cpp src/client/main.cpp src/client/gameview_impl.cpp src/client/gameview_render.cpp src/client/gameview_hud.cpp src/client/gameview_net.cpp src/client/gameview_lobby.cpp src/client/gameview_benchmark.cpp src/client/gameview_input.cpp src/client/gameview_voice.cpp src/client/ordertrail.cpp src/client/gameview_unitinfo.cpp src/client/font.cpp src/client/distantmodels.cpp src/client/animatedatlas.cpp src/client/mapview.cpp src/client/terrainchunks.cpp src/client/modelview.cpp src/client/replayfile.cpp src/client/mainmenu.cpp src/client/settings.cpp src/client/options.cpp src/client/hotkeys.cpp src/client/hotkeysscreen.cpp src/client/campaignscreen.cpp src/client/briefingscreen.cpp src/client/resultscreen.cpp src/client/loadscreen.cpp src/client/guiart.cpp src/client/cursors.cpp src/util/procmetrics.cpp src/util/relaunch.cpp src/video/bink.cpp src/video/stream.cpp src/client/streaming.cpp)
    # Local dev/test harness (standalone scenario/demo/FFA setup, --testbuild/--firetest...):
    # debug builds only. The file is also wrapped in #ifndef NDEBUG, as are its callers, so
    # this just skips an empty TU when the config's flags define NDEBUG.
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _tak_bt)
    if(NOT "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_tak_bt}}" MATCHES "NDEBUG")
      target_sources(takclient PRIVATE src/client/gameview_devharness.cpp)
    endif()
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
    # Voice chat (client/voice.h): the engine and the Opus codec, built from a pinned
    # source release (cmake/Opus.cmake). Client only -- takserver relays voice frames as
    # opaque bytes and never links Opus.
    include(cmake/Opus.cmake)
    add_library(tak-voice STATIC src/client/voice.cpp src/client/voicecodec.cpp)
    target_include_directories(tak-voice PUBLIC src)
    target_compile_definitions(tak-voice PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(tak-voice PUBLIC tak-formats ${_tak_sdl2} PRIVATE opus)
    # The codec, jitter buffer, push-to-talk rules, the engine on SDL's disk/dummy audio
    # drivers with synthetic PCM, the VOICE CHAT page (greyed while off) and the settings.
    add_executable(voice_test tools/voice_test.cpp src/client/options.cpp src/client/settings.cpp
      src/client/hotkeys.cpp)
    target_compile_definitions(voice_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(voice_test PRIVATE tak-voice tak-formats ${_tak_sdl2})
    add_test(NAME voice COMMAND voice_test)
    set_tests_properties(voice PROPERTIES TIMEOUT 60 ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    if(UNIX AND NOT APPLE)
      add_executable(override_settings_test tools/override_settings_test.cpp src/client/settings.cpp)
      target_link_libraries(override_settings_test PRIVATE tak-formats ${_tak_sdl2})
      add_test(NAME override_settings COMMAND override_settings_test)
    endif()
    # GAME DATA FOLDER (client/datafolder.h): precedence, picks, elision, restart
    # arguments for takclient and Cartographer, the Windows restart command line, and
    # the shared settings file (a private temp file via setSettingsPath).
    add_executable(datafolder_test tools/datafolder_test.cpp src/client/settings.cpp src/util/relaunch.cpp)
    target_compile_definitions(datafolder_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(datafolder_test PRIVATE tak-formats ${_tak_sdl2})
    add_test(NAME datafolder COMMAND datafolder_test)
    add_executable(crusades_ui_test tools/crusades_ui_test.cpp src/client/crusadesscreen.cpp src/client/font.cpp)
    target_compile_definitions(crusades_ui_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(crusades_ui_test PRIVATE tak-crusades-map ${_tak_sdl2})
    add_test(NAME crusades_ui COMMAND crusades_ui_test)
    set_tests_properties(crusades_ui PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy" TIMEOUT 60)
    add_executable(crusades_effective_audit tools/crusades_effective_audit.cpp)
    target_compile_definitions(crusades_effective_audit PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(crusades_effective_audit PRIVATE tak-formats ${_tak_sdl2})
    add_executable(stream_capture_test tools/stream_capture_test.cpp src/client/streaming.cpp src/video/stream.cpp)
    target_link_libraries(stream_capture_test PRIVATE tak-formats tak-credstore tak_ffmpeg ${_tak_sdl2})
    target_compile_definitions(stream_capture_test PRIVATE SDL_MAIN_HANDLED)
    add_test(NAME streaming_capture COMMAND stream_capture_test)
    set_tests_properties(streaming_capture PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy" TIMEOUT 15)
    # Remember Key logic and panel against a fake credential store (never the real one).
    add_executable(stream_key_test tools/stream_key_test.cpp src/client/streaming.cpp src/video/stream.cpp
                   src/client/settings.cpp)
    target_link_libraries(stream_key_test PRIVATE tak-formats tak-credstore tak_ffmpeg ${_tak_sdl2})
    target_compile_definitions(stream_key_test PRIVATE SDL_MAIN_HANDLED)
    add_test(NAME stream_key COMMAND stream_key_test)
    set_tests_properties(stream_key PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy" TIMEOUT 30)
    add_executable(cursor_test tools/cursor_test.cpp src/client/cursors.cpp)
    target_include_directories(cursor_test PRIVATE src)
    target_compile_definitions(cursor_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(cursor_test PRIVATE tak-formats ${_tak_sdl2})
    add_test(NAME cursor_roster COMMAND cursor_test)
    if(TAK_TEST_DATA AND CMAKE_BUILD_TYPE STREQUAL "Debug")
      add_test(NAME build_controls COMMAND ${CMAKE_COMMAND} -E env
          SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy TAK_CURSOR_TEST=1
          XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/ui-test-profile
          XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/ui-test-profile
          $<TARGET_FILE:takclient> game "Ulasem Arena" --data "${TAK_TEST_DATA}"
          --testbuild --winsize 1000 700 --shot "${CMAKE_CURRENT_BINARY_DIR}/build-controls.png")
      set_tests_properties(build_controls PROPERTIES TIMEOUT 90)
      add_test(NAME render_texture_reuse COMMAND ${CMAKE_COMMAND} -E env
          SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
          TAK_SHADOW_BENCH=16 TAK_SHADOW_BENCH_TYPE=arakeep TAK_PROFILE_ZOOM=0.5
          TAK_RENDER_STUDY=1 TAK_RENDER_FRAMES=180 TAK_GEOMETRY_VERIFY=1 TAK_REQUIRE_GEOMETRY_REUSE=1
          XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/render-test-profile
          XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/render-test-profile
          $<TARGET_FILE:takclient> game "Ulasem Arena" --data "${TAK_TEST_DATA}"
          --testbuild --winsize 1280 960 --novsync)
      set_tests_properties(render_texture_reuse PROPERTIES TIMEOUT 90)
      # Workstream M: every structure type at zoom 0.5 (distant-image bakes), construction
      # sites (special units), Texture Filtering toggled every 70 frames and render-target
      # resets: no failed indexed draw; on OpenGL indexed draws and switches both ways,
      # elsewhere the baked pages only (the ctest's dummy driver; run it with
      # SDL_VIDEODRIVER=offscreen SDL_RENDER_DRIVER=opengl for the GL form).
      add_test(NAME render_palette_indexed COMMAND ${CMAKE_COMMAND} -E env
          SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
          TAK_SHADOW_BENCH=160 TAK_RENDER_ROSTER=structures TAK_RENDER_SPACING=96 TAK_PROFILE_ZOOM=0.5
          TAK_RENDER_EXTRAS=1 TAK_RENDER_CAMERA=1 TAK_RENDER_FILTER=70 TAK_RENDER_RESET=110
          TAK_RENDER_STUDY=1 TAK_RENDER_FRAMES=300 TAK_REQUIRE_PALETTE=1 TAK_GEOMETRY_VERIFY=1
          XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/render-test-profile
          XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/render-test-profile
          $<TARGET_FILE:takclient> game "Ulasem Arena" --data "${TAK_TEST_DATA}"
          --testbuild --winsize 1280 960 --novsync)
      set_tests_properties(render_palette_indexed PROPERTIES TIMEOUT 120)
      # Workstream N: the same scene with native passes required and every pass's GL
      # state checked against SDL's priming. The ctest's dummy driver has no native
      # passes (the study requires none to open: the runtime fallback); run it with
      # SDL_VIDEODRIVER=offscreen SDL_RENDER_DRIVER=opengl for the GL form, where
      # passes must open across palette-form switches and render-target resets.
      add_test(NAME render_native_pass COMMAND ${CMAKE_COMMAND} -E env
          SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
          TAK_SHADOW_BENCH=160 TAK_RENDER_ROSTER=structures TAK_RENDER_SPACING=96 TAK_PROFILE_ZOOM=0.5
          TAK_RENDER_EXTRAS=1 TAK_RENDER_CAMERA=1 TAK_RENDER_FILTER=70 TAK_RENDER_RESET=110
          TAK_RENDER_STUDY=1 TAK_RENDER_FRAMES=300 TAK_REQUIRE_NATIVE_PASS=1 TAK_NATIVE_PASS_VERIFY=1
          XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/native-pass-profile
          XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/native-pass-profile
          $<TARGET_FILE:takclient> game "Ulasem Arena" --data "${TAK_TEST_DATA}"
          --testbuild --winsize 1280 960 --novsync)
      set_tests_properties(render_native_pass PROPERTIES TIMEOUT 120
          PASS_REGULAR_EXPRESSION "NATIVE_PASS_OK")
      # Animated colour atlases in a real scene (two player colours, simulated
      # render-target resets): after every update each atlas must equal a complete
      # repaint byte for byte (TAK_ATLAS_VERIFY; throws on the first mismatch).
      add_test(NAME render_atlas_verify COMMAND ${CMAKE_COMMAND} -E env
          SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
          TAK_SHADOW_BENCH=120 TAK_RENDER_BATTLE=1 TAK_PROFILE_ZOOM=0.75
          TAK_RENDER_STUDY=1 TAK_RENDER_FRAMES=130 TAK_RENDER_RESET=45 TAK_ATLAS_VERIFY=1
          XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/atlas-verify-profile
          XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/atlas-verify-profile
          $<TARGET_FILE:takclient> game "Ulasem Arena" --data "${TAK_TEST_DATA}"
          --testbuild --winsize 1024 768 --novsync)
      set_tests_properties(render_atlas_verify PROPERTIES TIMEOUT 120
          PASS_REGULAR_EXPRESSION "ATLAS_VERIFY checks=[1-9]")
      # Stable per-unit geometry entries in a real scene (workstream C): a moving,
      # panning army reorders in depth every frame, 5% dies and respawns every 30
      # frames, render targets reset every 70 and the world epoch advances every 110
      # (the id reuse of a rejoin); retention is shortened to 100 ms and released
      # memory is trimmed at once (debug knobs), so entries of units that left the
      # pan expire, are recycled and trimmed within the run. Every frame each visible
      # unit must hold a distinct entry owned by its own identity with no other
      # unit's content (TAK_GEOMETRY_OWNERSHIP_VERIFY), and every cache hit must
      # equal a rebuild (TAK_GEOMETRY_VERIFY); both throw at the first violation.
      add_test(NAME render_geometry_ownership COMMAND ${CMAKE_COMMAND} -E env
          SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
          TAK_SHADOW_BENCH=200 TAK_PROFILE_ZOOM=0.75 TAK_RENDER_MOTION=1 TAK_RENDER_CAMERA=1
          TAK_RENDER_CHURN=30 TAK_RENDER_RESET=70 TAK_RENDER_EPOCH=110
          TAK_GEOMETRY_RETAIN_MS=100 TAK_GEOMETRY_TRIM_MS=0
          TAK_RENDER_STUDY=1 TAK_RENDER_FRAMES=230 TAK_GEOMETRY_VERIFY=1 TAK_GEOMETRY_OWNERSHIP_VERIFY=1
          XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/geometry-ownership-profile
          XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/geometry-ownership-profile
          $<TARGET_FILE:takclient> game "Ulasem Arena" --data "${TAK_TEST_DATA}"
          --testbuild --winsize 800 600 --novsync)
      set_tests_properties(render_geometry_ownership PROPERTIES TIMEOUT 180
          PASS_REGULAR_EXPRESSION "OWNERSHIP_VERIFY checks=[1-9][0-9]* acquired=[1-9][0-9]* replaced=[1-9][0-9]* destroyed=[0-9]+ expired=[1-9][0-9]* budget=[0-9]+ trimmed=[1-9]")
      # Staged unit geometry in a real scene (workstream D): a battle with construction
      # sites, ghosts, flyers and corpses under a panning, zooming camera, so units are
      # re-placed (pan/zoom), re-coloured (conjuring, summoning, ghosts) and rebuilt
      # (animation). Every reuse, complete or partial, must equal a complete rebuild,
      # every local build must equal the former single-pass transform
      # (TAK_GEOMETRY_VERIFY) and every shadow the full collector (TAK_SHADOW_VERIFY);
      # each throws at the first mismatch.
      add_test(NAME render_geometry_stages COMMAND ${CMAKE_COMMAND} -E env
          SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
          TAK_SHADOW_BENCH=150 TAK_RENDER_BATTLE=1 TAK_RENDER_EXTRAS=1 TAK_PROFILE_ZOOM=1
          TAK_RENDER_CAMERA=1 TAK_RENDER_ZOOM=1
          TAK_RENDER_STUDY=1 TAK_RENDER_FRAMES=150 TAK_GEOMETRY_VERIFY=1 TAK_SHADOW_VERIFY=1
          XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/geometry-stages-profile
          XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/geometry-stages-profile
          $<TARGET_FILE:takclient> game "Ulasem Arena" --data "${TAK_TEST_DATA}"
          --testbuild --winsize 800 600 --novsync)
      set_tests_properties(render_geometry_stages PROPERTIES TIMEOUT 180
          PASS_REGULAR_EXPRESSION "GEOMETRY_STAGES hits=[0-9]+ placement=[1-9][0-9]* material=[1-9][0-9]* single_pass=[1-9]")
      # Shadow silhouette tile reuse in a real scene (workstream E), on SDL's software
      # renderer (TAK_SHADOW_ATLAS_SOFTWARE runs the atlas there): a stationary mixed
      # roster with construction, ghosts, flyers and corpses, animated textures with
      # and without shadow masks (aralode; tarnecro/tarwitch), a veterancy cycle
      # (material-only), a whole-pixel pan that pauses (translation and body-only
      # reuse) and render-target resets. Every frame each kept tile must equal a
      # complete re-bake pixel for pixel and every tile the pre-E key kept must be
      # kept (TAK_SHADOW_CACHE_VERIFY); every skipped silhouette must equal a fresh
      # collection and every reuse a complete rebuild (TAK_GEOMETRY_VERIFY).
      add_test(NAME render_shadow_cache COMMAND ${CMAKE_COMMAND} -E env
          SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software
          TAK_SHADOW_ATLAS_SOFTWARE=1 TAK_SHADOW_BENCH=80 TAK_PROFILE_ZOOM=1
          TAK_RENDER_ROSTER=araarch,zonter,versword,tarnecro,tarwitch,aralode TAK_RENDER_EXTRAS=1
          TAK_RENDER_CAMERA_SNAP=20 TAK_RENDER_VETERAN=25 TAK_RENDER_RESET=70
          TAK_RENDER_STUDY=1 TAK_RENDER_FRAMES=140 TAK_SHADOW_CACHE_VERIFY=1 TAK_GEOMETRY_VERIFY=1
          XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/shadow-cache-profile
          XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/shadow-cache-profile
          $<TARGET_FILE:takclient> game "Ulasem Arena" --data "${TAK_TEST_DATA}"
          --testbuild --winsize 1024 768 --novsync)
      set_tests_properties(render_shadow_cache PROPERTIES TIMEOUT 300
          PASS_REGULAR_EXPRESSION "shadow_skipped=[1-9][0-9]*\nSHADOW_CACHE_VERIFY frames=[1-9][0-9]* tiles=[0-9]+ reused=[0-9]+ body=[1-9][0-9]* translated=[1-9]")
      # Cost-planned pool passes (workstream I), on the software renderer with the
      # shadow atlas: a mixed roster that moves (local rebuilds), whole-pixel camera
      # steps (shadow point checks), a veterancy cycle, deaths/respawns (cold
      # entries) and render-target resets, on a 4-thread pool with policies that
      # make every pass parallel. Each planned geometry pass must build every
      # visible unit exactly once; the shadow checks, tile transforms and body
      # copies must equal a serial re-run bit for bit (TAK_SCHEDULE_VERIFY); every
      # reuse equals a complete rebuild (TAK_GEOMETRY_VERIFY).
      add_test(NAME render_schedule COMMAND ${CMAKE_COMMAND} -E env
          SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software
          TAK_SHADOW_ATLAS_SOFTWARE=1 TAK_SHADOW_BENCH=120 TAK_PROFILE_ZOOM=1
          TAK_RENDER_ROSTER=araarch,zonter,versword,tarnecro,tarwitch,aralode TAK_RENDER_EXTRAS=1
          TAK_RENDER_MOTION=1 TAK_RENDER_CAMERA_SNAP=20 TAK_RENDER_VETERAN=25 TAK_RENDER_CHURN=30
          TAK_RENDER_RESET=70 TAK_RENDER_POOL_THREADS=4
          TAK_SCHEDULE_GEOMETRY=0,4000,3000 TAK_SCHEDULE_SHADOW_CHECK=0,500,100
          TAK_SCHEDULE_SHADOW_PREP=0,500,100 TAK_SCHEDULE_COPY=0,500,100,4
          TAK_RENDER_STUDY=1 TAK_RENDER_FRAMES=140 TAK_SCHEDULE_VERIFY=1 TAK_GEOMETRY_VERIFY=1
          XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/schedule-profile
          XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/schedule-profile
          $<TARGET_FILE:takclient> game "Ulasem Arena" --data "${TAK_TEST_DATA}"
          --testbuild --winsize 1024 768 --novsync)
      set_tests_properties(render_schedule PROPERTIES TIMEOUT 300
          PASS_REGULAR_EXPRESSION "SCHEDULE_VERIFY frames=[1-9][0-9]* geometry_passes=[1-9][0-9]* builds=[1-9][0-9]* parallel_passes=[1-9][0-9]* checks=[1-9][0-9]* prep_passes=[1-9][0-9]* prep_points=[1-9][0-9]* copy_passes=[1-9][0-9]* copy_vertices=[1-9]")
      # Distant-image lookup before body generation (workstream G), on the software
      # renderer (TAK_DISTANT_SOFTWARE runs the image cache there): a mixed roster at
      # zoom 0.5 under a panning camera (held images re-placed, bodies deferred),
      # with a veterancy cycle, deaths/respawns and render-target resets. Every
      # deferred body is regenerated in a copy and must keep the entry's revision,
      # vertex count and shadow output, and its image must have been shown
      # (TAK_DISTANT_VERIFY); every generated body equals a complete rebuild
      # (TAK_GEOMETRY_VERIFY). No prediction may miss (catchup=0 late=0).
      add_test(NAME render_distant_lookup COMMAND ${CMAKE_COMMAND} -E env
          SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software
          TAK_DISTANT_SOFTWARE=1 TAK_SHADOW_BENCH=150 TAK_PROFILE_ZOOM=0.5 TAK_RENDER_CAMERA=1
          TAK_RENDER_VETERAN=25 TAK_RENDER_CHURN=40 TAK_RENDER_RESET=70
          TAK_RENDER_STUDY=1 TAK_RENDER_FRAMES=120 TAK_DISTANT_VERIFY=1 TAK_GEOMETRY_VERIFY=1
          XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/distant-lookup-profile
          XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/distant-lookup-profile
          $<TARGET_FILE:takclient> game "Ulasem Arena" --data "${TAK_TEST_DATA}"
          --testbuild --winsize 800 600 --novsync)
      set_tests_properties(render_distant_lookup PROPERTIES TIMEOUT 300
          PASS_REGULAR_EXPRESSION "DISTANT_VERIFY frames=[1-9][0-9]* deferred=[1-9][0-9]* checked=[1-9][0-9]* catchup=0 late=0")
      # Unit broad phase (workstream L), on the software renderer: every frame the
      # painter candidates taken from the snapshot's unit grid must equal a full scan
      # of all live units -- same units, order, depth keys and layers -- and so must
      # the units each per-unit overlay draws, or draw() throws
      # (TAK_UNIT_INDEX_VERIFY). The base variant is a fogged two-player battle with
      # construction, ghosts, flyers, corpses, self-destruct countdowns, control
      # squads, production queues and queued builds (the overlays and the snapshot's
      # build-ghost list), moving units (interpolation), a
      # panning and zooming camera with reversals, camera shake, deaths/respawns and
      # world-epoch bumps (id reuse); _jump is a 3000-unit army with squads seen
      # through a camera jumping between distant views (a small visible subset).
      foreach(_variant IN ITEMS "" "_jump")
        if(_variant STREQUAL "")
          set(_scene TAK_SHADOW_BENCH=300 TAK_RENDER_SPACING=120 TAK_RENDER_BATTLE=1 TAK_RENDER_EXTRAS=1
              TAK_RENDER_OVERLAYS=1 TAK_PROFILE_FOG=1 TAK_RENDER_MOTION=1 TAK_RENDER_CAMERA=1
              TAK_RENDER_ZOOM=1 TAK_RENDER_SHAKE=37 TAK_RENDER_CHURN=40 TAK_RENDER_EPOCH=90 TAK_RENDER_FRAMES=200)
          set(_expect "health=[1-9][0-9]* selfdestruct=[1-9][0-9]* squads=[1-9][0-9]* production=[1-9][0-9]* ghosts=[1-9]")
        else()
          set(_scene TAK_SHADOW_BENCH=3000 TAK_RENDER_OVERLAYS=1 TAK_PROFILE_ZOOM=1 TAK_RENDER_JUMP=15
              TAK_RENDER_SHAKE=23 TAK_RENDER_FRAMES=120)
          set(_expect "squads=[1-9]")
        endif()
        add_test(NAME render_unit_index${_variant} COMMAND ${CMAKE_COMMAND} -E env
            SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software ${_scene}
            TAK_RENDER_STUDY=1 TAK_UNIT_INDEX_VERIFY=1
            XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/unit-index${_variant}-profile
            XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/unit-index${_variant}-profile
            $<TARGET_FILE:takclient> game "Ulasem Arena" --data "${TAK_TEST_DATA}"
            --testbuild --winsize 1024 768 --novsync)
        set_tests_properties(render_unit_index${_variant} PROPERTIES TIMEOUT 300
            PASS_REGULAR_EXPRESSION "UNIT_INDEX_VERIFY frames=[1-9][0-9]* passes=[1-9][0-9]* live=[1-9][0-9]* candidates=[1-9][0-9]* accepted=[1-9][0-9]* .*${_expect}")
      endforeach()
      # CPU budgets (workstream J) in a real scene: a battle, then a sustained empty
      # view, with short trim windows and tiny terrain budgets. Retained buffers must
      # be trimmed after the demand falls, keep their contents byte for byte, never be
      # trimmed twice within a window, and the renderer's CPU memory must come down
      # (TAK_CPU_TRIM_VERIFY); every geometry reuse still equals a rebuild.
      add_test(NAME render_cpu_budget COMMAND ${CMAKE_COMMAND} -E env
          SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software
          TAK_SHADOW_BENCH=300 TAK_PROFILE_ZOOM=0.5 TAK_RENDER_BATTLE=1
          TAK_RENDER_SPARSE_AFTER=90 TAK_RENDER_SPARSE_OFFSET=8800,200
          TAK_CPU_TRIM_MS=400 TAK_CPU_TRIM_FRAMES=20 TAK_CPU_TRIM_FLOOR_KIB=1
          TAK_TERRAIN_SECTION_BUDGET_MIB=8 TAK_TERRAIN_SECTION_MIN_IDLE_MS=1000
          TAK_TERRAIN_MIP_BUDGET_MIB=2 TAK_TERRAIN_MIP_IDLE_MS=1000
          TAK_RENDER_STUDY=1 TAK_RENDER_FRAMES=200 TAK_CPU_TRIM_VERIFY=1 TAK_GEOMETRY_VERIFY=1
          XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/cpu-budget-profile
          XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/cpu-budget-profile
          $<TARGET_FILE:takclient> game "Ulasem Arena" --data "${TAK_TEST_DATA}"
          --testbuild --winsize 800 600 --novsync)
      set_tests_properties(render_cpu_budget PROPERTIES TIMEOUT 300
          PASS_REGULAR_EXPRESSION "CPU_TRIM_VERIFY frames=[1-9][0-9]* trims=[1-9][0-9]* .*content_checks=[1-9][0-9]* .*recovered=[1-9]")
      # Retained body vertices (workstream H) on the software renderer: the arena's
      # storage is simulated in CPU memory (TAK_BODY_ARENA_SOFTWARE) and its commands
      # are drawn by gathering their ranges, so the planner's bookkeeping runs in the
      # real body pass; TAK_BODY_ARENA_MIN=1 puts every command in the arena (small
      # segments and distant-image quads by value included). A mixed roster at zoom
      # 0.5 with a veterancy cycle, deaths/respawns and render-target resets. The base
      # scene alternates 20 moving / 20 still camera frames (streamed / retained); the
      # _still variant holds the camera on a tight storage (TAK_BODY_ARENA_HEADROOM)
      # so it rolls over: commands draw from the draining storage, migrate, and the
      # storage drains. Every arena command must equal the command assembled from its
      # sources, and every assembled command its sources with no gap in the copy
      # buffer (TAK_BODY_ARENA_VERIFY); no command may fall back. In the _fail
      # variant the first arena write with uploads in every 40 frames fails
      # (TAK_BODY_ARENA_FAIL_WRITE): that frame's arena commands are re-assembled in
      # place of the staged uploads.
      foreach(_variant IN ITEMS "" "_still" "_fail")
        if(_variant STREQUAL "_still")
          set(_arena_env TAK_BODY_ARENA_HEADROOM=1.2)
          set(_arena_pass "fallbacks=0 rollovers=[1-9][0-9]* drains=[1-9][0-9]* old_commands=[1-9].* assembled=[1-9]")
        elseif(_variant STREQUAL "_fail")
          set(_arena_env TAK_BODY_ARENA_FAIL_WRITE=40)
          set(_arena_pass "fallbacks=[1-9][0-9]* .* assembled=[1-9]")
        else()
          set(_arena_env TAK_RENDER_CAMERA_SNAP=20)
          set(_arena_pass "streams=[1-9][0-9]* fallbacks=0")
        endif()
        add_test(NAME render_body_arena${_variant} COMMAND ${CMAKE_COMMAND} -E env
            SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software
            TAK_BODY_ARENA_SOFTWARE=1 TAK_BODY_ARENA_MIN=1 ${_arena_env} TAK_DISTANT_SOFTWARE=1
            TAK_SHADOW_BENCH=150 TAK_PROFILE_ZOOM=0.5 TAK_RENDER_ROSTER=araarch,zonter,versword,aralode
            TAK_RENDER_VETERAN=25 TAK_RENDER_CHURN=40 TAK_RENDER_RESET=70
            TAK_RENDER_STUDY=1 TAK_RENDER_FRAMES=140 TAK_BODY_ARENA_VERIFY=1
            XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/body-arena${_variant}-profile
            XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/body-arena${_variant}-profile
            $<TARGET_FILE:takclient> game "Ulasem Arena" --data "${TAK_TEST_DATA}"
            --testbuild --winsize 800 600 --novsync)
        set_tests_properties(render_body_arena${_variant} PROPERTIES TIMEOUT 300
            PASS_REGULAR_EXPRESSION "BODY_ARENA_VERIFY frames=[1-9][0-9]* commands=[1-9][0-9]* vertices=[0-9]+ retained=[1-9][0-9]* uploaded=[1-9][0-9]* appends=[1-9][0-9]* rebuilds=[1-9][0-9]* .*${_arena_pass}")
      endforeach()
      # Persistent shadow tile cells (workstream F) under space pressure, on the
      # software renderer: the render_shadow_cache scene with 288-pixel pages, short
      # policy windows and deaths/respawns. render_shadow_atlas then moves the
      # camera so part of the army stays in view (cells expire, a sparse last page
      # is emptied by bounded migration and released, compaction); the _zoom
      # variant zooms continuously (cells outgrown every frame, free repacks,
      # retained cells evicted under pressure). Every kept tile must equal a
      # complete re-bake and the allocator must pass its consistency check every
      # frame (TAK_SHADOW_CACHE_VERIFY).
      # The _band variant holds a still army whose tiles fit four pages by area but
      # not by packing (every frame falls back), then goes sparse: a frame that does
      # not fit must create no page texture, so nothing is created and released over
      # and over while the view stays in that band (at most the sparse view's pages).
      foreach(_variant IN ITEMS "" "_zoom" "_band")
        if(_variant STREQUAL "")
          set(_scene TAK_RENDER_CAMERA_SNAP=20 TAK_PROFILE_ZOOM=1 TAK_RENDER_SPARSE_AFTER=100
              TAK_RENDER_SPARSE_OFFSET=5200,5000 TAK_RENDER_FRAMES=170)
          set(_expect "kept=[1-9][0-9]* allocs=[1-9][0-9]* compactions=[1-9][0-9]* migrated=[1-9][0-9]* evicted=[0-9]+ page_releases=[1-9].* validated=[1-9]")
        elseif(_variant STREQUAL "_zoom")
          set(_scene TAK_RENDER_ZOOM=1 TAK_RENDER_CAMERA=1 TAK_RENDER_FRAMES=120)
          set(_expect "kept=[0-9]+ allocs=[1-9][0-9]* compactions=[1-9][0-9]* migrated=[0-9]+ evicted=[1-9][0-9]*.* validated=[1-9]")
        else()
          set(_scene TAK_RENDER_SPARSE_AFTER=80 TAK_RENDER_SPARSE_OFFSET=5200,5000 TAK_RENDER_FRAMES=130)
          set(_expect "page_releases=[0-2] shrinks=[0-9]+ validated=[1-9][0-9]* page_allocs=[1-4] alloc_failures=[1-9][0-9]")
        endif()
        add_test(NAME render_shadow_atlas${_variant} COMMAND ${CMAKE_COMMAND} -E env
            SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software
            TAK_SHADOW_ATLAS_SOFTWARE=1 TAK_SHADOW_BENCH=80
            TAK_RENDER_ROSTER=araarch,zonter,versword,tarnecro,tarwitch,aralode TAK_RENDER_EXTRAS=1
            TAK_RENDER_VETERAN=25 TAK_RENDER_RESET=70 TAK_RENDER_CHURN=30 ${_scene}
            TAK_SHADOW_PAGE_MAX=288 TAK_SHADOW_ATLAS_DELAY_MS=100
            TAK_RENDER_STUDY=1 TAK_SHADOW_CACHE_VERIFY=1
            XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/shadow-atlas${_variant}-profile
            XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/shadow-atlas${_variant}-profile
            $<TARGET_FILE:takclient> game "Ulasem Arena" --data "${TAK_TEST_DATA}"
            --testbuild --winsize 1024 768 --novsync)
        set_tests_properties(render_shadow_atlas${_variant} PROPERTIES TIMEOUT 300
            PASS_REGULAR_EXPRESSION "SHADOW_CACHE_VERIFY frames=[1-9][0-9]* .* ${_expect}")
      endforeach()
      add_test(NAME tactical_dots COMMAND ${CMAKE_COMMAND} -E env
          SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy TAK_TACTICAL_DOTS_TEST=1
          XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/tactical-dots-profile
          XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/tactical-dots-profile
          $<TARGET_FILE:takclient> game "Ulasem Arena" --data "${TAK_TEST_DATA}"
          --testbuild --winsize 1280 960 --shot "${CMAKE_CURRENT_BINARY_DIR}/tactical-dots.png")
      set_tests_properties(tactical_dots PROPERTIES TIMEOUT 90
          PASS_REGULAR_EXPRESSION "PASS: tactical dots \\(threshold")
      add_test(NAME spectator_health_bars COMMAND ${CMAKE_COMMAND} -E env
          SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy TAK_SPECTATOR_BARS_TEST=1
          XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/spectator-bars-profile
          XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/spectator-bars-profile
          $<TARGET_FILE:takclient> game "Ulasem Arena" --data "${TAK_TEST_DATA}"
          --testbuild --winsize 1280 960 --shot "${CMAKE_CURRENT_BINARY_DIR}/spectator-bars.png")
      set_tests_properties(spectator_health_bars PROPERTIES TIMEOUT 90
          PASS_REGULAR_EXPRESSION "PASS: spectator bars \\(")
      add_test(NAME gui_smoothing_menu COMMAND ${CMAKE_COMMAND} -E env
          SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy TAK_GUI_SMOOTH_TEST=1
          XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/gui-smooth-menu-profile
          XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/gui-smooth-menu-profile
          $<TARGET_FILE:takclient> --data "${TAK_TEST_DATA}" --winsize 1280 960
          --shot "${CMAKE_CURRENT_BINARY_DIR}/gui-smooth-menu.png")
      set_tests_properties(gui_smoothing_menu PROPERTIES TIMEOUT 90
          PASS_REGULAR_EXPRESSION "PASS: title art smoothing applies live")
      add_test(NAME gui_smoothing_game COMMAND ${CMAKE_COMMAND} -E env
          SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy TAK_GUI_SMOOTH_TEST=1
          XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/gui-smooth-game-profile
          XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/gui-smooth-game-profile
          $<TARGET_FILE:takclient> game "Ulasem Arena" --data "${TAK_TEST_DATA}"
          --testbuild --winsize 1280 960 --shot "${CMAKE_CURRENT_BINARY_DIR}/gui-smooth-game.png")
      set_tests_properties(gui_smoothing_game PROPERTIES TIMEOUT 90
          PASS_REGULAR_EXPRESSION "PASS: HUD smoothing applies live")
      # Options > GAME DATA FOLDER through real input and a fake picker (invalid, cancel,
      # valid + restart prompt, LATER, RESTART NOW, the locked form), and the menu's own
      # row greyed by this launch's --data. The test writes the settings file, so it is
      # named outright: SDL_GetPrefPath ignores XDG_* on Windows and macOS.
      add_test(NAME options_datafolder COMMAND ${CMAKE_COMMAND} -E env
          SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy TAK_OPTIONS_DATADIR_TEST=1
          XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/options-datafolder-profile
          XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/options-datafolder-profile
          TAK_SETTINGS_FILE=${CMAKE_CURRENT_BINARY_DIR}/options-datafolder-settings.ini
          $<TARGET_FILE:takclient> --data "${TAK_TEST_DATA}" --winsize 1280 960
          --shot "${CMAKE_CURRENT_BINARY_DIR}/options-datafolder.png")
      set_tests_properties(options_datafolder PROPERTIES TIMEOUT 90
          PASS_REGULAR_EXPRESSION "PASS: Options data folder")
      # RESTART NOW end to end: the client execs itself and the new process mounts the
      # new folder (POSIX exec; Windows starts a second process, not testable this way).
      if(Python3_EXECUTABLE AND NOT WIN32)
        add_test(NAME client_restart COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_SOURCE_DIR}/tools/client_restart_test.py" "$<TARGET_FILE:takclient>" "${TAK_TEST_DATA}")
        set_tests_properties(client_restart PROPERTIES TIMEOUT 150)
      endif()
      # A menu single-player launch whose local server cannot run must come back to
      # the title screen with the reason (not the multiplayer sign-in dialog, not a
      # silent exit); the --shot then captures that screen and exits.
      add_test(NAME menu_launch_failure COMMAND ${CMAKE_COMMAND} -E env
          SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy TAK_MENU_PICK=singleplayer
          "TAK_LOCAL_SERVER_BIN=${CMAKE_CURRENT_BINARY_DIR}/no-such-takserver"
          XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/menu-launch-failure-profile
          XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/menu-launch-failure-profile
          $<TARGET_FILE:takclient> menu --data "${TAK_TEST_DATA}" --winsize 1024 768
          --shot "${CMAKE_CURRENT_BINARY_DIR}/menu-launch-failure.png")
      set_tests_properties(menu_launch_failure PROPERTIES TIMEOUT 60
          PASS_REGULAR_EXPRESSION "menu: launch failed -> notice: could not (run|start) the local server")
      # These isolated UI processes own SDL's DBus initialization; release its
      # global cache at shutdown so leak checks can see engine allocations.
      set_tests_properties(build_controls render_texture_reuse render_atlas_verify render_geometry_ownership
          render_geometry_stages render_schedule render_shadow_cache render_shadow_atlas render_shadow_atlas_zoom render_shadow_atlas_band render_distant_lookup render_cpu_budget render_body_arena render_body_arena_still render_body_arena_fail render_unit_index render_unit_index_jump
          tactical_dots spectator_health_bars
          gui_smoothing_menu gui_smoothing_game
          menu_launch_failure options_datafolder PROPERTIES ENVIRONMENT "SDL_SHUTDOWN_DBUS_ON_QUIT=1")
    endif()
    add_executable(shadow_test tools/shadow_test.cpp)
    target_include_directories(shadow_test PRIVATE src)
    target_compile_definitions(shadow_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(shadow_test PRIVATE ${_tak_sdl2})
    add_test(NAME shadow COMMAND shadow_test)
    add_executable(render_telemetry_test tools/render_telemetry_test.cpp)
    target_include_directories(render_telemetry_test PRIVATE src)
    target_compile_definitions(render_telemetry_test PRIVATE SDL_MAIN_HANDLED)
    # ThreadPool telemetry checks; SDL for the vertex types in client/geometrystages.h.
    target_link_libraries(render_telemetry_test PRIVATE Threads::Threads ${_tak_sdl2})
    add_test(NAME render_telemetry COMMAND render_telemetry_test)
    # Cost-planned worker-pool passes (workstream I): plans follow the estimates and
    # caps; passes equal the serial loop; only requested workers wake; late workers
    # never touch a finished pass.
    add_executable(work_schedule_test tools/work_schedule_test.cpp)
    target_include_directories(work_schedule_test PRIVATE src)
    target_link_libraries(work_schedule_test PRIVATE Threads::Threads)
    add_test(NAME work_schedule COMMAND work_schedule_test)
    # Stable generational ownership of per-unit geometry entries (workstream C).
    # Retained-buffer trim policy (client/cpubudget.h, workstream J).
    add_executable(cpu_budget_test tools/cpu_budget_test.cpp)
    target_include_directories(cpu_budget_test PRIVATE src)
    add_test(NAME cpu_budget COMMAND cpu_budget_test)
    add_executable(geometry_owners_test tools/geometry_owners_test.cpp)
    target_include_directories(geometry_owners_test PRIVATE src)
    target_link_libraries(geometry_owners_test PRIVATE Threads::Threads)
    add_test(NAME geometry_owners COMMAND geometry_owners_test)
    # Staged unit geometry (workstream D): placement/material arithmetic equals the
    # former single pass; partial stage updates equal a complete rebuild.
    add_executable(geometry_stages_test tools/geometry_stages_test.cpp)
    target_include_directories(geometry_stages_test PRIVATE src)
    target_compile_definitions(geometry_stages_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(geometry_stages_test PRIVATE ${_tak_sdl2})
    add_test(NAME geometry_stages COMMAND geometry_stages_test)
    # Shadow silhouette tile reuse (workstream E): a tile is kept exactly when its
    # raster inputs (pre-E expressions) are bit-identical, across every invalidation
    # cause; body-only local inputs are separated from the shadow's.
    add_executable(shadow_tiles_test tools/shadow_tiles_test.cpp)
    target_include_directories(shadow_tiles_test PRIVATE src)
    target_compile_definitions(shadow_tiles_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(shadow_tiles_test PRIVATE ${_tak_sdl2})
    add_test(NAME shadow_tiles COMMAND shadow_tiles_test)
    # Persistent shadow tile allocation (workstream F): the sequential mode equals
    # the former per-frame packer slot for slot; randomized churn never overlaps
    # cells or moves a live one; generational handles; full page reclamation.
    add_executable(shadow_atlas_test tools/shadow_atlas_test.cpp)
    target_include_directories(shadow_atlas_test PRIVATE src)
    add_test(NAME shadow_atlas COMMAND shadow_atlas_test)
    # Unit broad phase (workstream L): grid queries are supersets of brute force, in
    # ascending order; the band-to-world rectangle keeps every unit the renderer's
    # exact float cull accepts, edges included; randomized movement and churn.
    add_executable(unit_index_test tools/unit_index_test.cpp)
    target_include_directories(unit_index_test PRIVATE src)
    add_test(NAME unit_index COMMAND unit_index_test)
    # Workstream M: structure palette sheet layout, sheet-id coordinates and the
    # indexed decode against the baked expansion (pure, no SDL).
    add_executable(palette_sheets_test tools/palette_sheets_test.cpp)
    target_include_directories(palette_sheets_test PRIVATE src)
    add_test(NAME palette_sheets COMMAND palette_sheets_test)
    # ... and the indexed GL program against the baked pages (pixel-identical NEAREST);
    # GL runs need TAK_GEOMETRY_TEST_ACCELERATED=1, the ctest checks the software refusal.
    add_executable(palette_indexed_test tools/palette_indexed_test.cpp)
    target_include_directories(palette_indexed_test PRIVATE src)
    target_compile_definitions(palette_indexed_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(palette_indexed_test PRIVATE ${_tak_sdl2})
    add_test(NAME palette_indexed COMMAND palette_indexed_test)
    # Workstream N: native passes against SDL drawing the same commands, with the GL
    # state checked after every pass; GL runs need TAK_GEOMETRY_TEST_ACCELERATED=1, the
    # ctest checks the software refusal.
    add_executable(native_pass_test tools/native_pass_test.cpp)
    target_include_directories(native_pass_test PRIVATE src)
    target_compile_definitions(native_pass_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(native_pass_test PRIVATE ${_tak_sdl2})
    add_test(NAME native_pass COMMAND native_pass_test)
    add_executable(geometrysubmit_test tools/geometrysubmit_test.cpp)
    target_include_directories(geometrysubmit_test PRIVATE src)
    target_compile_definitions(geometrysubmit_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(geometrysubmit_test PRIVATE ${_tak_sdl2})
    add_test(NAME geometrysubmit COMMAND geometrysubmit_test)
    # Workstream H: the retained vertex arena planner against a simulated buffer.
    add_executable(vertex_arena_test tools/vertex_arena_test.cpp)
    target_include_directories(vertex_arena_test PRIVATE src)
    target_compile_definitions(vertex_arena_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(vertex_arena_test PRIVATE ${_tak_sdl2})
    add_test(NAME vertex_arena COMMAND vertex_arena_test)
    add_executable(fogupload_test tools/fogupload_test.cpp)
    target_include_directories(fogupload_test PRIVATE src)
    target_compile_definitions(fogupload_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(fogupload_test PRIVATE ${_tak_sdl2})
    add_test(NAME fogupload COMMAND fogupload_test)
    set_tests_properties(fogupload PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    add_executable(audio_device_test tools/audio_device_test.cpp src/client/options.cpp src/client/settings.cpp
      src/client/hotkeys.cpp)
    target_compile_definitions(audio_device_test PRIVATE SDL_MAIN_HANDLED
      SDL_GetNumAudioDevices=TAK_Test_GetNumAudioDevices
      SDL_OpenAudioDevice=TAK_Test_OpenAudioDevice)
    target_link_libraries(audio_device_test PRIVATE tak-formats ${_tak_sdl2})
    add_test(NAME audio_device COMMAND audio_device_test)
    add_executable(renderdriver_test tools/renderdriver_test.cpp)
    target_include_directories(renderdriver_test PRIVATE src)
    target_compile_definitions(renderdriver_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(renderdriver_test PRIVATE ${_tak_sdl2})
    add_test(NAME renderdriver COMMAND renderdriver_test)
    add_executable(menu_music_test tools/menu_music_test.cpp)
    target_compile_definitions(menu_music_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(menu_music_test PRIVATE tak-formats ${_tak_sdl2})
    add_test(NAME menu_music COMMAND menu_music_test)
    if(TAK_TEST_DATA)
      add_test(NAME menu_music_roster COMMAND menu_music_test "${TAK_TEST_DATA}")
    endif()
    add_executable(death_sound_test tools/death_sound_test.cpp)
    target_compile_definitions(death_sound_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(death_sound_test PRIVATE tak-formats ${_tak_sdl2})
    add_test(NAME death_sound COMMAND death_sound_test)
    add_executable(sound_hit_test tools/sound_hit_test.cpp)
    target_compile_definitions(sound_hit_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(sound_hit_test PRIVATE tak-formats ${_tak_sdl2})
    add_test(NAME sound_hit COMMAND sound_hit_test)
    if(TAK_TEST_DATA)
      add_test(NAME sound_hit_data COMMAND sound_hit_test "${TAK_TEST_DATA}")
    endif()
    add_executable(campaign_audio_test tools/campaign_audio_test.cpp)
    target_compile_definitions(campaign_audio_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(campaign_audio_test PRIVATE tak-formats ${_tak_sdl2})
    add_test(NAME campaign_audio COMMAND campaign_audio_test)
    add_executable(terrain_cache_test tools/terrain_cache_test.cpp src/client/mapview.cpp src/client/terrainchunks.cpp)
    target_include_directories(terrain_cache_test PRIVATE src)
    target_compile_definitions(terrain_cache_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(terrain_cache_test PRIVATE tak-formats ${_tak_sdl2})
    if(TAK_TEST_DATA)
      add_test(NAME terrain_cache COMMAND terrain_cache_test "${TAK_TEST_DATA}")
      set_tests_properties(terrain_cache PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    endif()
    # Camera-driven terrain work in a real MapView (workstream K): priority, cancellation
    # with hysteresis, stand-ins under memory pressure, nearby work, edits/resets mid-queue.
    add_executable(terrain_schedule_test tools/terrain_schedule_test.cpp src/client/mapview.cpp src/client/terrainchunks.cpp)
    target_include_directories(terrain_schedule_test PRIVATE src)
    target_compile_definitions(terrain_schedule_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(terrain_schedule_test PRIVATE tak-formats ${_tak_sdl2})
    if(TAK_TEST_DATA)
      add_test(NAME terrain_schedule COMMAND terrain_schedule_test "${TAK_TEST_DATA}")
      set_tests_properties(terrain_schedule PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    endif()
    # Decoded terrain sections: pinned lifetimes, LRU budget with watermarks,
    # redecode accounting, concurrent pins (synthetic JPEGs, no game data).
    add_executable(terrain_compositor_test tools/terrain_compositor_test.cpp)
    target_include_directories(terrain_compositor_test PRIVATE src)
    target_link_libraries(terrain_compositor_test PRIVATE tak-formats Threads::Threads)
    add_test(NAME terrain_compositor COMMAND terrain_compositor_test)
    # Camera-driven terrain work policy (workstream K): tiers, priority, keep band and
    # level grace, stand-in eviction ranks, the shared upload budget, queue simulations.
    add_executable(terrain_work_test tools/terrain_work_test.cpp)
    target_include_directories(terrain_work_test PRIVATE src)
    add_test(NAME terrain_work COMMAND terrain_work_test)
    # Animated colour-atlas regions: incremental repaint == complete repaint ==
    # the previous repaint-everything scheme (frames, gutters, expiry, late atlas,
    # renderer reset). Software renderer under the dummy video driver.
    add_executable(animated_atlas_test tools/animated_atlas_test.cpp src/client/animatedatlas.cpp)
    target_include_directories(animated_atlas_test PRIVATE src)
    target_compile_definitions(animated_atlas_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(animated_atlas_test PRIVATE ${_tak_sdl2})
    add_test(NAME animated_atlas COMMAND animated_atlas_test)
    set_tests_properties(animated_atlas PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_SHUTDOWN_DBUS_ON_QUIT=1")
    add_executable(distant_models_test tools/distant_models_test.cpp src/client/distantmodels.cpp)
    target_include_directories(distant_models_test PRIVATE src)
    target_compile_definitions(distant_models_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(distant_models_test PRIVATE ${_tak_sdl2})
    add_test(NAME distant_models COMMAND distant_models_test)
    set_tests_properties(distant_models PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_SHUTDOWN_DBUS_ON_QUIT=1")
    add_executable(campaign_presentation_test tools/campaign_presentation_test.cpp
        src/client/campaignscreen.cpp src/client/font.cpp src/client/guiart.cpp)
    target_compile_definitions(campaign_presentation_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(campaign_presentation_test PRIVATE tak-formats ${_tak_sdl2})
    add_test(NAME campaign_presentation COMMAND campaign_presentation_test)
    add_executable(screen_lifecycle_test tools/screen_lifecycle_test.cpp
        src/client/loadscreen.cpp src/client/resultscreen.cpp src/client/campaignscreen.cpp
        src/client/font.cpp src/client/guiart.cpp src/client/cursors.cpp src/video/bink.cpp)
    target_compile_definitions(screen_lifecycle_test PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(screen_lifecycle_test PRIVATE tak-formats tak_ffmpeg ${_tak_sdl2})
    if(TAK_TEST_DATA)
      add_test(NAME screen_lifecycle COMMAND screen_lifecycle_test "${TAK_TEST_DATA}")
      set_tests_properties(screen_lifecycle PROPERTIES
          ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" TIMEOUT 60)
      add_test(NAME campaign_presentation_roster COMMAND campaign_presentation_test "${TAK_TEST_DATA}")
      add_test(NAME campaign_audio_roster COMMAND campaign_audio_test "${TAK_TEST_DATA}")
      add_test(NAME death_sound_roster COMMAND death_sound_test "${TAK_TEST_DATA}")
      add_executable(firing_sound_test tools/firing_sound_test.cpp)
      target_compile_definitions(firing_sound_test PRIVATE SDL_MAIN_HANDLED)
      target_link_libraries(firing_sound_test PRIVATE tak-formats ${_tak_sdl2})
      add_test(NAME firing_sound_roster COMMAND firing_sound_test "${TAK_TEST_DATA}")
      add_executable(crusades_registry_test tools/crusades_registry_test.cpp)
      target_compile_definitions(crusades_registry_test PRIVATE SDL_MAIN_HANDLED)
      target_link_libraries(crusades_registry_test PRIVATE tak-formats ${_tak_sdl2})
      add_test(NAME crusades_registry COMMAND crusades_registry_test "${TAK_TEST_DATA}")
    endif()
    add_executable(crusades_damage_test tools/crusades_damage_test.cpp)
    target_link_libraries(crusades_damage_test PRIVATE tak-formats)
    add_test(NAME crusades_damage COMMAND crusades_damage_test)
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
    # The accepted-site BeginLanding producer has a distinct ZONROC wing pose
    # from BeginFlight, so keep its mission-to-COB-to-model join independently
    # repeatable when the local retail assets and emulator are available.
    if(TAK_TEST_DATA AND EXISTS "${TAK_TEST_DATA}/KINGDOMS.icd" AND
       EXISTS "${CMAKE_SOURCE_DIR}/assets/extracted/all/scripts/zonroc.cob" AND
       EXISTS "${CMAKE_SOURCE_DIR}/assets/extracted/all/objects3d/zonroc.3do")
      find_package(Python3 COMPONENTS Interpreter QUIET)
      if(Python3_Interpreter_FOUND)
        execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import unicorn"
          RESULT_VARIABLE _tak_zonroc_landing_unicorn_status
          OUTPUT_QUIET ERROR_QUIET)
        if("${_tak_zonroc_landing_unicorn_status}" STREQUAL "0")
          add_test(NAME native_zonroc_landing_pose
            COMMAND "${Python3_EXECUTABLE}"
              "${CMAKE_SOURCE_DIR}/tools/re/check_native_zonroc_landing_pose.py"
              --script-binary "$<TARGET_FILE:retail_script_test>"
              --model-binary "$<TARGET_FILE:model_transform_test>"
              --icd "${TAK_TEST_DATA}/KINGDOMS.icd")
          set_tests_properties(native_zonroc_landing_pose PROPERTIES TIMEOUT 60)
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

    target_link_libraries(takclient PRIVATE tak-crusades-map tak-formats tak-gpustats tak-credstore tak-voice ${_tak_sdl2})
    # We provide our own main() and call SDL_SetMainReady(). Define SDL_MAIN_HANDLED for
    # the WHOLE target (not just main.cpp) so it applies before ANY header pulls in
    # <SDL.h> -- otherwise, on Windows, SDL_main.h renames our main to SDL_main via a
    # transitive include and the MinGW link fails with "undefined reference to WinMain".
    # TAK_PRODUCT: a shipped program. Its Release build keeps SDL's per-user folder
    # off Linux even under XDG_DATA_HOME, which test executables honour (util/prefpath.h).
    target_compile_definitions(takclient PRIVATE SDL_MAIN_HANDLED TAK_PRODUCT)
    if(TARGET tak_ffmpeg)
      target_link_libraries(takclient PRIVATE tak_ffmpeg)
    else()
      message(STATUS "FFmpeg not found — menu .bik videos disabled (GAF door art fallback).")
    endif()
    if(WIN32)
      # WinMain forwards to our SDL_MAIN_HANDLED main without allocating a console.
      set_target_properties(takclient PROPERTIES WIN32_EXECUTABLE TRUE)
      enable_language(RC)
      configure_file(packaging/windows/takclient.rc.in
                     "${CMAKE_CURRENT_BINARY_DIR}/takclient.rc" @ONLY)
      target_sources(takclient PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/takclient.rc")
      set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/takclient.rc" PROPERTIES
          OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/res/icons/takclient.ico")
    endif()

    add_executable(cartographer_document_test tools/cartographer_document_test.cpp
      src/cartographer/recovery.cpp src/cartographer/document.cpp src/cartographer/history.cpp src/cartographer/units.cpp src/cartographer/sections.cpp src/cartographer/preferences.cpp src/cartographer/sectiongroups.cpp
      src/cartographer/symmetry.cpp src/cartographer/regions.cpp src/cartographer/triggers.cpp)
    target_link_libraries(cartographer_document_test PRIVATE tak-formats)
    add_test(NAME cartographer_document COMMAND cartographer_document_test)

    add_executable(cartographer_savejob_test tools/cartographer_savejob_test.cpp
      src/cartographer/savejob.cpp src/cartographer/document.cpp src/cartographer/newmap.cpp
      src/cartographer/sections.cpp src/cartographer/units.cpp)
    target_link_libraries(cartographer_savejob_test PRIVATE tak-formats)
    add_test(NAME cartographer_savejob COMMAND cartographer_savejob_test)

    add_executable(cartographer_playtest_test tools/cartographer_playtest_test.cpp
      src/cartographer/playtest.cpp src/cartographer/document.cpp src/cartographer/units.cpp)
    target_link_libraries(cartographer_playtest_test PRIVATE tak-formats)
    add_test(NAME cartographer_playtest COMMAND cartographer_playtest_test)

    add_executable(cartographer_validation_test tools/cartographer_validation_test.cpp
      src/cartographer/validation.cpp src/cartographer/triggers.cpp src/cartographer/regions.cpp src/cartographer/generator.cpp src/cartographer/overlay.cpp src/cartographer/ruleedit.cpp)
    target_link_libraries(cartographer_validation_test PRIVATE tak-formats)
    add_test(NAME cartographer_validation COMMAND cartographer_validation_test)
    add_executable(cartographer_features_test tools/cartographer_features_test.cpp src/cartographer/features.cpp)
    target_link_libraries(cartographer_features_test PRIVATE tak-formats)
    add_test(NAME cartographer_feature_art COMMAND cartographer_features_test)

    # Cartographer -- the clean-room map editor (docs/cartographer-port.md).
    # Shares the engine's terrain render stack (mapview + tak-formats) so maps
    # look byte-identical to the game.
    add_executable(cartographer src/client/modelview.cpp src/util/relaunch.cpp src/cartographer/savejob.cpp src/cartographer/playtest.cpp src/cartographer/editorfont.cpp src/cartographer/thumbnails.cpp src/cartographer/recovery.cpp src/cartographer/preferences.cpp src/cartographer/ruleedit.cpp src/cartographer/overlay.cpp src/cartographer/generator.cpp src/cartographer/regions.cpp src/cartographer/validation.cpp src/cartographer/history.cpp src/client/settings.cpp src/cartographer/document.cpp src/cartographer/main.cpp src/cartographer/session.cpp src/cartographer/headless.cpp src/cartographer/palette.cpp src/cartographer/canvas.cpp src/cartographer/chrome.cpp src/cartographer/dialogs.cpp src/cartographer/scripting.cpp src/cartographer/symmetrytools.cpp src/cartographer/symmetry.cpp src/cartographer/sections.cpp src/cartographer/sectiongroups.cpp src/cartographer/newmap.cpp src/cartographer/features.cpp src/cartographer/units.cpp src/cartographer/triggers.cpp src/client/mapview.cpp src/client/terrainchunks.cpp)
    target_link_libraries(cartographer PRIVATE tak-formats ${_tak_sdl2})
    target_compile_definitions(cartographer PRIVATE SDL_MAIN_HANDLED TAK_PRODUCT)   # see takclient
    get_target_property(_cartographer_sources cartographer SOURCES)
    add_executable(cartographer_ui_test tools/cartographer_ui_test.cpp ${_cartographer_sources})
    target_compile_definitions(cartographer_ui_test PRIVATE SDL_MAIN_HANDLED TAK_CARTOGRAPHER_TEST)
    target_link_libraries(cartographer_ui_test PRIVATE tak-formats ${_tak_sdl2})
    add_test(NAME cartographer_font COMMAND cartographer_ui_test font)
    set_tests_properties(cartographer_font PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_SHUTDOWN_DBUS_ON_QUIT=1")
    if(TAK_TEST_DATA)
      add_test(NAME cartographer_ui COMMAND cartographer_ui_test "${TAK_TEST_DATA}")
      add_test(NAME cartographer_generation_ui COMMAND cartographer_ui_test "${TAK_TEST_DATA}" generation)
      add_test(NAME cartographer_overlay_ui COMMAND cartographer_ui_test "${TAK_TEST_DATA}" overlay)
      add_test(NAME cartographer_objects_ui COMMAND cartographer_ui_test "${TAK_TEST_DATA}" objects)
      add_test(NAME cartographer_model_ui COMMAND cartographer_ui_test "${TAK_TEST_DATA}" model)
      add_test(NAME cartographer_thumbnails COMMAND cartographer_ui_test "${TAK_TEST_DATA}" thumbnails)
      add_test(NAME cartographer_minimap_ui COMMAND cartographer_ui_test "${TAK_TEST_DATA}" minimap)
      add_test(NAME cartographer_features_ui COMMAND cartographer_ui_test "${TAK_TEST_DATA}" features)
      add_test(NAME cartographer_resize_save_ui COMMAND cartographer_ui_test "${TAK_TEST_DATA}" resize-save)
      add_test(NAME cartographer_templates_ui COMMAND cartographer_ui_test "${TAK_TEST_DATA}" templates)
      add_test(NAME cartographer_rules_ui COMMAND cartographer_ui_test "${TAK_TEST_DATA}" rules)
      add_test(NAME cartographer_regions_ui COMMAND cartographer_ui_test "${TAK_TEST_DATA}" regions)
      add_test(NAME cartographer_validation_ui COMMAND cartographer_ui_test "${TAK_TEST_DATA}" validation)
      add_test(NAME cartographer_headless_ui COMMAND cartographer_ui_test "${TAK_TEST_DATA}" headless)
      add_test(NAME cartographer_symmetry_ui COMMAND cartographer_ui_test "${TAK_TEST_DATA}" symmetry)
      add_test(NAME cartographer_terrain_clipboard_ui COMMAND cartographer_ui_test "${TAK_TEST_DATA}" terrain-clipboard)
      add_test(NAME cartographer_sections_ui COMMAND cartographer_ui_test "${TAK_TEST_DATA}" sections)
      add_test(NAME cartographer_palette_scroll_ui COMMAND cartographer_ui_test "${TAK_TEST_DATA}" palette-scroll)
      add_test(NAME cartographer_display_ui COMMAND cartographer_ui_test "${TAK_TEST_DATA}" display)
      add_test(NAME cartographer_datafolder_ui COMMAND cartographer_ui_test "${TAK_TEST_DATA}" datafolder)
      set_tests_properties(cartographer_palette_scroll_ui cartographer_display_ui cartographer_datafolder_ui PROPERTIES TIMEOUT 120 ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_SHUTDOWN_DBUS_ON_QUIT=1")
      find_package(Python3 COMPONENTS Interpreter QUIET)
      if(Python3_Interpreter_FOUND AND UNIX AND NOT APPLE)
        add_test(NAME cartographer_recovery COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_SOURCE_DIR}/tools/cartographer_recovery_test.py" "$<TARGET_FILE:cartographer_ui_test>" "${TAK_TEST_DATA}")
        set_tests_properties(cartographer_recovery PROPERTIES TIMEOUT 70)
      endif()
      set_tests_properties(cartographer_objects_ui cartographer_model_ui cartographer_templates_ui cartographer_resize_save_ui cartographer_rules_ui cartographer_ui cartographer_generation_ui cartographer_overlay_ui cartographer_regions_ui cartographer_features_ui cartographer_minimap_ui cartographer_thumbnails cartographer_validation_ui cartographer_headless_ui cartographer_symmetry_ui cartographer_terrain_clipboard_ui cartographer_sections_ui PROPERTIES TIMEOUT 90 ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_SHUTDOWN_DBUS_ON_QUIT=1")
    endif()
    # Placement and robustness: every browser item placed through the real click
    # path, the STARTS crash regression, every feature operation and a UI smoke pass.
    add_executable(cartographer_placement_test tools/cartographer_placement_test.cpp ${_cartographer_sources})
    target_compile_definitions(cartographer_placement_test PRIVATE SDL_MAIN_HANDLED TAK_CARTOGRAPHER_TEST)
    target_link_libraries(cartographer_placement_test PRIVATE tak-formats ${_tak_sdl2})
    if(TAK_TEST_DATA)
      add_test(NAME cartographer_starts_ui COMMAND cartographer_placement_test "${TAK_TEST_DATA}" starts)
      add_test(NAME cartographer_browser_rescan COMMAND cartographer_placement_test "${TAK_TEST_DATA}" rescan)
      add_test(NAME cartographer_place_slots COMMAND cartographer_placement_test "${TAK_TEST_DATA}" slots)
      add_test(NAME cartographer_place_features COMMAND cartographer_placement_test "${TAK_TEST_DATA}" features)
      add_test(NAME cartographer_place_units COMMAND cartographer_placement_test "${TAK_TEST_DATA}" units)
      add_test(NAME cartographer_place_blocked COMMAND cartographer_placement_test "${TAK_TEST_DATA}" blocked)
      set(_cartographer_placement_tests cartographer_starts_ui cartographer_browser_rescan cartographer_place_slots
          cartographer_place_features cartographer_place_units cartographer_place_blocked)
      add_test(NAME cartographer_ui_smoke COMMAND cartographer_placement_test "${TAK_TEST_DATA}" smoke)
      list(APPEND _cartographer_placement_tests cartographer_ui_smoke)
      foreach(_world aramon veruna taros zhon creon)
        add_test(NAME cartographer_place_terrain_${_world} COMMAND cartographer_placement_test "${TAK_TEST_DATA}" terrain ${_world})
        add_test(NAME cartographer_feature_ops_${_world} COMMAND cartographer_placement_test "${TAK_TEST_DATA}" feature-ops ${_world})
        list(APPEND _cartographer_placement_tests cartographer_place_terrain_${_world} cartographer_feature_ops_${_world})
      endforeach()
      set_tests_properties(${_cartographer_placement_tests} PROPERTIES TIMEOUT 900 SKIP_RETURN_CODE 77 LABELS cartographer
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_SHUTDOWN_DBUS_ON_QUIT=1")
    endif()
    if(WIN32)
      set_target_properties(cartographer PROPERTIES WIN32_EXECUTABLE TRUE)
      configure_file(packaging/windows/cartographer.rc.in
                     "${CMAKE_CURRENT_BINARY_DIR}/cartographer.rc" @ONLY)
      target_sources(cartographer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/cartographer.rc")
      set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/cartographer.rc" PROPERTIES
          OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/res/icons/cartographer.ico")
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

# Strategic campaign definitions, state and territory rules deliberately have
# no tactical simulation dependency. Persistence has a separate library.
add_library(tak-crusades STATIC src/server/crusades/campaign.cpp src/server/crusades/rules.cpp)
target_include_directories(tak-crusades PUBLIC src)
add_executable(crusades_import tools/crusades_import.cpp)
target_link_libraries(crusades_import PRIVATE tak-crusades-map tak-crusades)
if(WIN32)
  target_link_libraries(crusades_import PRIVATE shell32)
endif()
add_executable(crusades_campaign_test tools/crusades_campaign_test.cpp)
target_link_libraries(crusades_campaign_test PRIVATE tak-crusades)
add_test(NAME crusades_campaign COMMAND crusades_campaign_test)
add_executable(crusades_rules_test tools/crusades_rules_test.cpp)
target_link_libraries(crusades_rules_test PRIVATE tak-crusades)
add_test(NAME crusades_rules COMMAND crusades_rules_test)

include(cmake/SQLite.cmake)
add_library(tak-crusades-servicelease STATIC src/server/crusades/servicelease.cpp)
target_include_directories(tak-crusades-servicelease PUBLIC src)
add_executable(crusades_servicelease_test tools/crusades_servicelease_test.cpp)
target_link_libraries(crusades_servicelease_test PRIVATE tak-crusades-servicelease)
if(WIN32)
  target_link_libraries(crusades_servicelease_test PRIVATE shell32)
endif()
add_test(NAME crusades_servicelease COMMAND crusades_servicelease_test)
set_tests_properties(crusades_servicelease PROPERTIES TIMEOUT 30)
add_library(tak-crusades-store STATIC src/server/crusades/store.cpp src/server/crusades/matchmaking.cpp)
target_link_libraries(tak-crusades-store PUBLIC tak-crusades PRIVATE tak-sqlite tak-crypto)
add_executable(crusades_admin tools/crusades_admin.cpp)
target_link_libraries(crusades_admin PRIVATE tak-crusades-store tak-crusades-servicelease)
if(WIN32)
  target_link_libraries(crusades_admin PRIVATE shell32)
endif()
add_executable(crusades_admin_test tools/crusades_admin_test.cpp)
target_link_libraries(crusades_admin_test PRIVATE tak-crusades-store tak-sqlite)
add_test(NAME crusades_admin COMMAND crusades_admin_test)
set_tests_properties(crusades_admin PROPERTIES TIMEOUT 90)
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME crowdbench_matrix COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tools/crowdbench_matrix_test.py")
  add_test(NAME render_benchmark_tool COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tools/render_benchmark_test.py")
  # AR-11: crowdbench `unreachable` 200x1, per-unit approach arrival and order completion; every
  # order completes on arrival at its unit's nearest reachable spot, all by tick 3000 (W2 step 2).
  add_test(NAME legion_unreachable_orders COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tools/legion_unreachable_test.py" "$<TARGET_FILE:crowdbench>" --require-w2)
  set_tests_properties(legion_unreachable_orders PROPERTIES LABELS "legion;w2" TIMEOUT 300)
  # One passage, one admission (2026-10-19): crowdbench `opposingbridges` 100x1, half of a lane's bodies sent each
  # way through a 6-cell gap 64 cells long. The gap crosses two sector boundaries, two opening records with a token
  # each: each stream took the record at its own end and both were admitted into the one strip (0 of 100 arrived,
  # all 100 held). One record now holds the admission of the passage; at least 90 arrive.
  add_test(NAME legion_passage_records COMMAND crowdbench --mode legion --scenario opposingbridges --units 100
           --players 1 --moving-percent 100 --ticks 6000 --seed 0)
  set_tests_properties(legion_passage_records PROPERTIES LABELS legion TIMEOUT 300
                       PASS_REGULAR_EXPRESSION "\"physical_in_goal\":(9[0-9]|100),")
  add_test(NAME navigation_determinism COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tools/navigation_determinism_test.py"
    "$<TARGET_FILE:navigation_checkpoint_test>"
    "${CMAKE_SOURCE_DIR}/tools/navigation_checkpoints.json")
  set_tests_properties(navigation_determinism PROPERTIES TIMEOUT 1200)
  add_test(NAME crusades_admin_cli COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tools/crusades_admin_cli_test.py" "$<TARGET_FILE:crusades_admin>")
  set_tests_properties(crusades_admin_cli PROPERTIES TIMEOUT 90)
endif()
add_executable(crusades_store_test tools/crusades_store_test.cpp)
target_link_libraries(crusades_store_test PRIVATE tak-crusades-store tak-sqlite)
add_test(NAME crusades_store COMMAND crusades_store_test)
set_tests_properties(crusades_store PROPERTIES TIMEOUT 90)
add_executable(crusades_allegiance_test tools/crusades_allegiance_test.cpp)
target_link_libraries(crusades_allegiance_test PRIVATE tak-crusades-store tak-sqlite)
add_test(NAME crusades_allegiance COMMAND crusades_allegiance_test)
add_executable(crusades_battle_test tools/crusades_battle_test.cpp)
target_link_libraries(crusades_battle_test PRIVATE tak-crusades-store tak-sqlite)
add_test(NAME crusades_battle COMMAND crusades_battle_test)
add_executable(crusades_matchmaking_test tools/crusades_matchmaking_test.cpp)
target_link_libraries(crusades_matchmaking_test PRIVATE tak-crusades-store)
add_test(NAME crusades_matchmaking COMMAND crusades_matchmaking_test)
add_executable(crusades_result_test tools/crusades_result_test.cpp)
target_link_libraries(crusades_result_test PRIVATE tak-crusades-store tak-sqlite)
add_test(NAME crusades_result COMMAND crusades_result_test)
add_executable(crusades_history_test tools/crusades_history_test.cpp)
target_link_libraries(crusades_history_test PRIVATE tak-crusades-store tak-sqlite)
add_test(NAME crusades_history COMMAND crusades_history_test)
add_executable(crusades_rules_store_test tools/crusades_rules_store_test.cpp)
target_link_libraries(crusades_rules_store_test PRIVATE tak-crusades-store tak-sqlite)
add_test(NAME crusades_rules_store COMMAND crusades_rules_store_test)
add_library(tak-crusades-allegiance STATIC src/server/crusades/allegiance.cpp)
target_link_libraries(tak-crusades-allegiance PUBLIC tak-crusades-store PRIVATE tak-formats)
add_library(tak-crusades-network STATIC src/server/crusades/network.cpp src/server/crusades/replayfiles.cpp)
target_link_libraries(tak-crusades-network PUBLIC tak-crusades-store PRIVATE tak-formats)
target_link_libraries(crusades_admin PRIVATE tak-crusades-network)
add_executable(crusades_protocol_test tools/crusades_protocol_test.cpp)
target_link_libraries(crusades_protocol_test PRIVATE tak-formats)
add_test(NAME crusades_protocol COMMAND crusades_protocol_test)
add_executable(crusades_network_service_test tools/crusades_network_service_test.cpp)
target_link_libraries(crusades_network_service_test PRIVATE tak-crusades-network tak-formats tak-sqlite)
add_test(NAME crusades_network_service COMMAND crusades_network_service_test)
add_executable(crusades_replay_files_test tools/crusades_replay_files_test.cpp)
target_link_libraries(crusades_replay_files_test PRIVATE tak-crusades-network tak-formats)
add_test(NAME crusades_replay_files COMMAND crusades_replay_files_test)
add_executable(crusades_client_network_test tools/crusades_client_network_test.cpp)
target_link_libraries(crusades_client_network_test PRIVATE tak-formats)
add_test(NAME crusades_client_network COMMAND crusades_client_network_test)
set_tests_properties(crusades_client_network PROPERTIES TIMEOUT 60)
# ALLOW SPECTATORS as a real MpClient sees it (scripted loopback server, no data).
add_executable(spectator_client_test tools/spectator_client_test.cpp)
target_link_libraries(spectator_client_test PRIVATE tak-formats)
add_test(NAME spectator_client COMMAND spectator_client_test)
add_executable(voice_client_test tools/voice_client_test.cpp)
target_link_libraries(voice_client_test PRIVATE tak-formats)
add_test(NAME voice_client COMMAND voice_client_test)
add_executable(voice_uplink_test tools/voice_uplink_test.cpp)
target_link_libraries(voice_uplink_test PRIVATE tak-formats)
add_test(NAME voice_uplink COMMAND voice_uplink_test)
set_tests_properties(voice_uplink PROPERTIES TIMEOUT 150)
set_tests_properties(spectator_client PROPERTIES TIMEOUT 60)
# TAK_PACELOG's summary (src/client/pacelog.h, header-only).
add_executable(pacelog_test tools/pacelog_test.cpp)
target_include_directories(pacelog_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME pacelog COMMAND pacelog_test)
add_executable(replay_file_test tools/replay_file_test.cpp src/client/replayfile.cpp)
target_link_libraries(replay_file_test PRIVATE tak-formats)
add_test(NAME replay_file COMMAND replay_file_test)
# The streamed replay writer and the recovery of a recording a crash left unfinished.
add_executable(replay_writer_test tools/replay_writer_test.cpp src/client/replayfile.cpp)
target_link_libraries(replay_writer_test PRIVATE tak-formats)
add_test(NAME replay_writer COMMAND replay_writer_test)
add_executable(match_codec_test tools/match_codec_test.cpp src/client/replayfile.cpp)
target_link_libraries(match_codec_test PRIVATE tak-formats)
add_test(NAME match_codec COMMAND match_codec_test)

# The skirmish AI (src/ai) is SERVER-ONLY -- it never links into takclient, so the
# client cannot run AI locally. AIs run only on the server.
add_library(tak-acme STATIC src/server/acme.cpp)
target_include_directories(tak-acme PRIVATE "${TAK_FFMPEG_PREFIX}/include")
target_link_libraries(tak-acme PUBLIC tak-formats tak-crusades-servicelease)
add_executable(acme_test tools/acme_test.cpp)
target_link_libraries(acme_test PRIVATE tak-acme)
target_include_directories(acme_test PRIVATE "${TAK_FFMPEG_PREFIX}/include")
add_test(NAME acme COMMAND acme_test)
set_tests_properties(acme PROPERTIES TIMEOUT 30)
add_executable(acme_test_driver tools/acme_test_driver.cpp)
target_link_libraries(acme_test_driver PRIVATE tak-acme)
add_executable(takserver src/server/server.cpp src/server/status.cpp src/server/accounts.cpp src/server/roomtick.cpp src/ai/ai.cpp)
target_link_libraries(takserver PRIVATE tak-acme tak-formats tak-crusades-allegiance tak-crusades-network tak-crusades-servicelease)
if(APPLE)
  target_link_libraries(takserver PRIVATE "-framework CoreFoundation")
endif()
if(Python3_Interpreter_FOUND)
  add_test(NAME server_status_cli COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/tools/server_status_test.py" --server "$<TARGET_FILE:takserver>")
  set_tests_properties(server_status_cli PROPERTIES TIMEOUT 30)
endif()
if(TAK_TEST_DATA)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME override_transfer COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/override_transfer_test.py"
      --server "$<TARGET_FILE:takserver>" --client "$<TARGET_FILE:override_transfer_test>" --data "${TAK_TEST_DATA}")
    set_tests_properties(override_transfer PROPERTIES TIMEOUT 150)
    add_test(NAME crusades_hardening_network COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tools/crusades_hardening_network_test.py"
      --server "$<TARGET_FILE:takserver>" --data "${TAK_TEST_DATA}" --admin "$<TARGET_FILE:crusades_admin>")
    set_tests_properties(crusades_hardening_network PROPERTIES TIMEOUT 180)
    add_test(NAME server_command_flood COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tools/server_command_flood_test.py"
      --server "$<TARGET_FILE:takserver>" --data "${TAK_TEST_DATA}")
    set_tests_properties(server_command_flood PROPERTIES TIMEOUT 60)
    add_test(NAME server_orders_network COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tools/server_orders_network_test.py"
      --server "$<TARGET_FILE:takserver>" --data "${TAK_TEST_DATA}")
    set_tests_properties(server_orders_network PROPERTIES TIMEOUT 120)
    add_test(NAME server_seed COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tools/server_seed_test.py"
      --server "$<TARGET_FILE:takserver>" --data "${TAK_TEST_DATA}")
    set_tests_properties(server_seed PROPERTIES TIMEOUT 120)
    add_test(NAME server_speed_pacing COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tools/server_speed_pacing_test.py"
      --server "$<TARGET_FILE:takserver>" --data "${TAK_TEST_DATA}")
    set_tests_properties(server_speed_pacing PROPERTIES TIMEOUT 120)
    add_test(NAME server_public COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tools/server_public_test.py"
      --server "$<TARGET_FILE:takserver>" --data "${TAK_TEST_DATA}" --tls-tool "$<TARGET_FILE:tls_test>" --map-tool "$<TARGET_FILE:map_transfer_test>")
    set_tests_properties(server_public PROPERTIES TIMEOUT 120)
    add_test(NAME server_spectators COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tools/server_spectators_test.py"
      --server "$<TARGET_FILE:takserver>" --data "${TAK_TEST_DATA}")
    set_tests_properties(server_spectators PROPERTIES TIMEOUT 120)
    add_test(NAME server_voice COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tools/server_voice_test.py"
      --server "$<TARGET_FILE:takserver>" --data "${TAK_TEST_DATA}")
    set_tests_properties(server_voice PROPERTIES TIMEOUT 120)
    # Voice to a listener on a slow link (and, --case cpu, to a slow machine, which keeps it).
    add_test(NAME server_voice_slow COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tools/server_voice_slow_test.py"
      --server "$<TARGET_FILE:takserver>" --data "${TAK_TEST_DATA}")
    add_test(NAME server_voice_cpu COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tools/server_voice_slow_test.py"
      --server "$<TARGET_FILE:takserver>" --data "${TAK_TEST_DATA}" --case cpu)
    set_tests_properties(server_voice_slow server_voice_cpu PROPERTIES TIMEOUT 120)
    add_test(NAME server_map_scenario COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tools/server_map_scenario_test.py"
      --server "$<TARGET_FILE:takserver>" --data "${TAK_TEST_DATA}" --map-tool "$<TARGET_FILE:map_transfer_test>")
    set_tests_properties(server_map_scenario PROPERTIES TIMEOUT 180)
    add_test(NAME server_status COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tools/server_status_test.py"
      --server "$<TARGET_FILE:takserver>" --data "${TAK_TEST_DATA}" --tls-tool "$<TARGET_FILE:tls_test>")
    set_tests_properties(server_status PROPERTIES TIMEOUT 120)
    add_test(NAME crusades_auth_network COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tools/crusades_auth_network_test.py"
      --server "$<TARGET_FILE:takserver>" --data "${TAK_TEST_DATA}")
    set_tests_properties(crusades_auth_network PROPERTIES TIMEOUT 120)
    add_test(NAME crusades_battle_network COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tools/crusades_battle_network_test.py"
      --server "$<TARGET_FILE:takserver>" --data "${TAK_TEST_DATA}")
    set_tests_properties(crusades_battle_network PROPERTIES TIMEOUT 120)
    add_test(NAME crusades_result_network COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tools/crusades_result_network_test.py"
      --server "$<TARGET_FILE:takserver>" --data "${TAK_TEST_DATA}")
    set_tests_properties(crusades_result_network PROPERTIES TIMEOUT 120)
    add_test(NAME crusades_history_network COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tools/crusades_history_network_test.py"
      --server "$<TARGET_FILE:takserver>" --data "${TAK_TEST_DATA}")
    set_tests_properties(crusades_history_network PROPERTIES TIMEOUT 180)
    add_test(NAME crusades_protocol_network COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tools/crusades_protocol_network_test.py"
      --server "$<TARGET_FILE:takserver>" --data "${TAK_TEST_DATA}")
    set_tests_properties(crusades_protocol_network PROPERTIES TIMEOUT 120)
    add_test(NAME crusades_matchmaking_network COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_SOURCE_DIR}/tools/crusades_matchmaking_network_test.py"
      --server "$<TARGET_FILE:takserver>" --data "${TAK_TEST_DATA}")
    set_tests_properties(crusades_matchmaking_network PROPERTIES TIMEOUT 180)
    if(TARGET takclient AND CMAKE_BUILD_TYPE STREQUAL "Debug")
      # Replays are saved however a game ends: crash, kill, play on after the result,
      # SIGTERM, a dead server (the headless harness is debug-only).
      add_test(NAME replay_always COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/replay_always_test.py"
        --server "$<TARGET_FILE:takserver>" --client "$<TARGET_FILE:takclient>" --data "${TAK_TEST_DATA}")
      set_tests_properties(replay_always PROPERTIES TIMEOUT 300)
    endif()
    if(TARGET takclient AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
      add_test(NAME crusades_ui_network COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/crusades_ui_network_test.py"
        --server "$<TARGET_FILE:takserver>" --client "$<TARGET_FILE:takclient>" --data "${TAK_TEST_DATA}")
      set_tests_properties(crusades_ui_network PROPERTIES TIMEOUT 180)
      add_test(NAME crusades_history_ui_network COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/crusades_history_ui_network_test.py"
        --server "$<TARGET_FILE:takserver>" --client "$<TARGET_FILE:takclient>" --data "${TAK_TEST_DATA}")
      set_tests_properties(crusades_history_ui_network PROPERTIES TIMEOUT 240)
      add_test(NAME crusades_matchmaking_ui_network COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/tools/crusades_matchmaking_network_test.py"
        --server "$<TARGET_FILE:takserver>" --client "$<TARGET_FILE:takclient>" --data "${TAK_TEST_DATA}")
      set_tests_properties(crusades_matchmaking_ui_network PROPERTIES TIMEOUT 240)
    endif()
  endif()
endif()

add_executable(modeltool tools/modeltool.cpp)
target_link_libraries(modeltool PRIVATE tak-formats)

add_executable(tdftool tools/tdftool.cpp)
target_link_libraries(tdftool PRIVATE tak-formats)

add_executable(cobtool tools/cobtool.cpp)
target_link_libraries(cobtool PRIVATE tak-formats)

# --- Packaging: native .deb + .rpm via CPack (Linux) --------------------------
# One source of truth for both package families. Build, then from the build dir:
#   cpack -G DEB   -> tak-engine-<version>-{amd64,arm64}.deb
#   cpack -G RPM   -> tak-engine-<version>-{x86_64,aarch64}.rpm
# Every build bakes SDL2/jpeg/zlib and the Bink FFmpeg in, so SHLIBDEPS/AUTOREQ below
# auto-detect only the C runtime.
# SDL2 still dlopens its X11/Wayland/audio backends, which every desktop that can run the
# game already has; those are not declared (they aren't link-time deps of a static SDL2).
if(UNIX AND NOT APPLE)
  # Ship the service without enabling it. ACME is an opt-in, edited drop-in.
  install(FILES packaging/systemd/takserver.service DESTINATION lib/systemd/system)
  install(FILES packaging/systemd/takserver-acme.conf packaging/systemd/takserver-settings.conf
                packaging/systemd/README.md
          DESTINATION share/doc/tak-engine/systemd)
  install(FILES STREAMING-LICENSES.txt EDITOR-FONT-LICENSE.txt ACME-LICENSES.txt OPUS-LICENSE.txt DESTINATION share/licenses/tak-engine)
  install(TARGETS takserver crusades_admin RUNTIME DESTINATION bin)
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
  include(cmake/LinuxPackageArchitecture.cmake)
  tak_linux_package_architecture("${CMAKE_SYSTEM_PROCESSOR}" _tak_deb_arch _tak_rpm_arch)
  set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${_tak_deb_arch}")
  set(CPACK_RPM_PACKAGE_ARCHITECTURE "${_tak_rpm_arch}")
  set(CPACK_DEBIAN_FILE_NAME "tak-engine-${PROJECT_VERSION}-${_tak_dtag}${_tak_deb_arch}.deb")
  set(CPACK_RPM_FILE_NAME "tak-engine-${PROJECT_VERSION}-${_tak_dtag}${_tak_rpm_arch}.rpm")

  # Auto-detect the shared-library dependencies for each family.
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_SECTION "games")
  set(CPACK_RPM_PACKAGE_AUTOREQ ON)
  set(CPACK_RPM_PACKAGE_GROUP "Amusements/Games")

  include(CPack)
elseif(WIN32)
  install(FILES STREAMING-LICENSES.txt EDITOR-FONT-LICENSE.txt ACME-LICENSES.txt OPUS-LICENSE.txt DESTINATION .)
  # Windows installer (NSIS via CPack; the CI's MSYS2 job runs `cpack -G NSIS`).
  # Binaries install into the chosen folder root (no bin/), with Start-menu
  # shortcuts and an uninstaller. The static build has no DLLs to carry.
  install(TARGETS takserver crusades_admin RUNTIME DESTINATION .)
  if(TARGET takclient)
    install(TARGETS takclient RUNTIME DESTINATION .)
  endif()

  set(TAK_PACKAGE_CARTOGRAPHER OFF)
  if(TARGET cartographer)
    install(TARGETS cartographer RUNTIME DESTINATION .)
    set(TAK_PACKAGE_CARTOGRAPHER ON)
  endif()
  include(packaging/windows/installer.cmake)

  include(CPack)
elseif(APPLE)
  install(TARGETS takserver crusades_admin RUNTIME DESTINATION bin)
endif()

# Per-user files. Every test gets its own XDG_DATA_HOME, XDG_CONFIG_HOME and (Linux)
# HOME under test-profiles/<test>/, unless its ENVIRONMENT names that variable itself,
# so whatever a test writes to the per-user folder (util/prefpath.h: settings.ini,
# replays, Cartographer's preferences and recovery files) stays in the build dir. The
# per-user folder follows XDG_DATA_HOME on every platform in Debug builds and test
# executables; SDL alone honours it only on Linux. On 2026-10-14 diplomacy_ui and four
# more takclient tests, which set no XDG_DATA_HOME, saved their --data folder as the
# player's dataDir in the real ~/.local/share/TAKengine/TAKingdoms/settings.ini. A new
# test needs nothing to be safe. The tak_prefs fixture brackets every run:
# pref_guard_check fails if anything wrote a TAKengine folder into a test's sentinel
# HOME (a process that dropped its XDG_DATA_HOME, i.e. the real profile outside ctest);
# pref_guard_lint fails on an SDL_GetPrefPath call outside prefpath.h;
# pref_guard_selftest replays the leak and checks that the guard names it. The defaults
# are applied at the end of this file (cmake_language DEFER), so a test added or an
# ENVIRONMENT set anywhere below is covered too.
set(TAK_TEST_PROFILES "${CMAKE_CURRENT_BINARY_DIR}/test-profiles")
set(TAK_PREF_GUARD "${CMAKE_SOURCE_DIR}/tools/pref_guard.cmake")
add_test(NAME pref_guard_setup COMMAND ${CMAKE_COMMAND} -DMODE=setup "-DPROFILES=${TAK_TEST_PROFILES}" -P "${TAK_PREF_GUARD}")
add_test(NAME pref_guard_check COMMAND ${CMAKE_COMMAND} -DMODE=check "-DPROFILES=${TAK_TEST_PROFILES}" -P "${TAK_PREF_GUARD}")
set_tests_properties(pref_guard_setup PROPERTIES FIXTURES_SETUP tak_prefs)
set_tests_properties(pref_guard_check PROPERTIES FIXTURES_CLEANUP tak_prefs)
add_test(NAME pref_guard_lint COMMAND ${CMAKE_COMMAND} -DMODE=lint "-DSOURCE=${CMAKE_SOURCE_DIR}" -P "${TAK_PREF_GUARD}")
if(TARGET takclient AND TAK_TEST_DATA AND CMAKE_BUILD_TYPE STREQUAL "Debug" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_test(NAME pref_guard_selftest COMMAND ${CMAKE_COMMAND} -DMODE=selftest "-DCLIENT=$<TARGET_FILE:takclient>"
           "-DDATA=${TAK_TEST_DATA}" "-DROOT=${CMAKE_CURRENT_BINARY_DIR}/pref-guard-selftest" -P "${TAK_PREF_GUARD}")
  set_tests_properties(pref_guard_selftest PROPERTIES TIMEOUT 60)
endif()
# A scratch HOME hides the user's own Python modules (unicorn, for the native_*
# emulation checks); keep them importable.
set(tak_python_userbase "")
if(UNIX AND NOT APPLE AND Python3_EXECUTABLE)
  execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import site; print(site.getuserbase())"
                  OUTPUT_VARIABLE tak_python_userbase OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
function(tak_isolate_test_profiles)
  get_property(tak_all_tests DIRECTORY PROPERTY TESTS)
  foreach(tak_test IN LISTS tak_all_tests)
    if(tak_test MATCHES "^pref_guard_(setup|check)$")
      continue()
    endif()
    set(tak_profile "${TAK_TEST_PROFILES}/${tak_test}")
    set(tak_defaults "XDG_DATA_HOME=${tak_profile}/data" "XDG_CONFIG_HOME=${tak_profile}/config"
                     "XDG_CACHE_HOME=${TAK_TEST_PROFILES}/cache")
    if(UNIX AND NOT APPLE)
      list(APPEND tak_defaults "HOME=${tak_profile}/home")
      if(tak_python_userbase)
        list(APPEND tak_defaults "PYTHONUSERBASE=${tak_python_userbase}")
      endif()
      # The scratch HOME also hides the global git config, where CI records the checkout as a
      # safe.directory; without it git refuses the source tree as "dubious ownership" and
      # legion_doc_links cannot tell that CI's clone is shallow.
      list(APPEND tak_defaults "GIT_CONFIG_COUNT=1" "GIT_CONFIG_KEY_0=safe.directory"
                               "GIT_CONFIG_VALUE_0=${CMAKE_SOURCE_DIR}")
    endif()
    # Plain ENVIRONMENT (not ENVIRONMENT_MODIFICATION), so runners that read only that,
    # like the Windows host's runctest.py, isolate tests too.
    get_test_property(${tak_test} ENVIRONMENT tak_env)
    if(NOT tak_env)
      set(tak_env "")
    endif()
    set(tak_merged "")
    foreach(tak_pair IN LISTS tak_defaults)
      string(REGEX MATCH "^[A-Z_]+" tak_var "${tak_pair}")
      if(NOT tak_env MATCHES "(^|;)${tak_var}=")
        list(APPEND tak_merged "${tak_pair}")
      endif()
    endforeach()
    list(APPEND tak_merged ${tak_env})
    set_property(TEST ${tak_test} PROPERTY ENVIRONMENT ${tak_merged})
    set_property(TEST ${tak_test} APPEND PROPERTY FIXTURES_REQUIRED tak_prefs)
  endforeach()
endfunction()
cmake_language(DEFER CALL tak_isolate_test_profiles)

# Test scheduling and the `quick` label. tools/ctest_costs.txt lists the tests that take
# 5 s or more on an optimized Debug build (`<test> <seconds>`, longest first). Each gets a
# COST so ctest starts the longest first (ctest's own CTestCostData.txt only exists after a
# first run in a build dir; this is the fresh-checkout default and the lower bound that
# keeps a long test from being scheduled last). Every other test that is not `nightly` is
# labelled `quick`: `ctest -L quick -j N` is the targeted run for task agents (about a
# minute of wall time at -j 8; it still covers every area). Refresh the file after adding a
# slow test: tools/ctest_costs.py build-o2 (see docs/development.md).
set(TAK_CTEST_COSTS "${CMAKE_SOURCE_DIR}/tools/ctest_costs.txt")
if(EXISTS "${TAK_CTEST_COSTS}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${TAK_CTEST_COSTS}")
  file(STRINGS "${TAK_CTEST_COSTS}" tak_cost_lines)
  set(tak_slow_tests "")
  foreach(tak_line IN LISTS tak_cost_lines)
    if(tak_line MATCHES "^([A-Za-z0-9_.+-]+)[ \t]+([0-9.]+)")
      if(TEST ${CMAKE_MATCH_1})
        set_tests_properties(${CMAKE_MATCH_1} PROPERTIES COST ${CMAKE_MATCH_2})
        list(APPEND tak_slow_tests ${CMAKE_MATCH_1})
      endif()
    endif()
  endforeach()
  get_property(tak_all_tests DIRECTORY PROPERTY TESTS)
  foreach(tak_test IN LISTS tak_all_tests)
    if(NOT tak_test IN_LIST tak_slow_tests)
      get_test_property(${tak_test} LABELS tak_labels)
      if(NOT tak_labels MATCHES "nightly")
        set_property(TEST ${tak_test} APPEND PROPERTY LABELS quick)
      endif()
    endif()
  endforeach()
endif()
