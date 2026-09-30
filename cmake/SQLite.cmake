# Pinned public-domain SQLite amalgamation. Compile it ourselves on every
# platform: no system sqlite shared library or additional shipped DLL is used.
enable_language(C)
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
