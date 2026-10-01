# Pinned public-domain SQLite amalgamation. Compile it ourselves on every
# platform: no system sqlite shared library or additional shipped DLL is used.
enable_language(C)
# CMake appends C's implicit libraries when this archive joins a C++ link.
# Fedora names its shared unwinder gcc_s_asneeded, unlike C++'s gcc_s, so
# the extra -l can override the Linux -static-libgcc policy. Leave the driver
# to select that runtime and retain all other implicit C libraries.
if(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  list(FILTER CMAKE_C_IMPLICIT_LINK_LIBRARIES EXCLUDE REGEX "^gcc_s(_asneeded)?$")
endif()
include(FetchContent)
FetchContent_Declare(tak_sqlite
  URL https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
  URL_HASH SHA256=1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d
  TLS_VERIFY TRUE
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# For offline builds, set FETCHCONTENT_SOURCE_DIR_TAK_SQLITE to an extracted
# copy of this amalgamation (containing sqlite3.c and sqlite3.h).
FetchContent_MakeAvailable(tak_sqlite)
find_package(Threads REQUIRED)
add_library(tak-sqlite STATIC "${tak_sqlite_SOURCE_DIR}/sqlite3.c")
target_include_directories(tak-sqlite SYSTEM PUBLIC "${tak_sqlite_SOURCE_DIR}")
target_compile_definitions(tak-sqlite PRIVATE
  SQLITE_THREADSAFE=1
  SQLITE_OMIT_LOAD_EXTENSION
)
set_target_properties(tak-sqlite PROPERTIES POSITION_INDEPENDENT_CODE ON C_STANDARD 99)
target_link_libraries(tak-sqlite PRIVATE Threads::Threads)
