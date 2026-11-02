# TAK Engine 0.9.18

0.9.18 follows 0.9.17. It is a clean-up release: the engine now builds with no compiler
warnings on Linux, Windows and macOS, and it was checked with the full set of tests,
including the local and remote multiplayer desync sweeps. On Windows, a single-player
game's private server now always closes with the game. Nothing about how the game
plays changed.

**Compatibility.** 0.9.18 keeps network protocol **309** and replay format **15**, so
**0.9.18, 0.9.17 and 0.9.16 play together in either direction, and each plays the
others' replays.** A server needs no update. 0.9.15 and earlier stay refused.

## A clean build

0.9.17's builds printed about 390 distinct compiler warnings across the platforms. None
of them was a bug in the game. Now there are none:

- **Silenced as noise:** aggregate initialisers that leave the remaining fields at
  zero on purpose, and three known false reports from GCC (a sort of at most eight
  entries, and two reports that only GCC 11 and 12 make).
- **Replaced:** the C++20-deprecated `std::filesystem::u8path`, used for every file
  path that comes from text, is now a small helper on C++20's own UTF-8 path
  constructor. It reads the same names on every platform, including non-English
  folder names on Windows.
- **Fixed without changing behaviour:** unused parameters and variables, signed and
  unsigned comparisons, statements sharing a line with an `if`, copies in loops, a
  `printf` size, the Windows GPU counter checks, and the TLS certificate holders, which
  now use a lock instead of the deprecated atomic shared-pointer calls.
- **Hardened:** password hashing (PBKDF2) refuses a salt within four bytes of the
  largest possible size, where its buffer size would have wrapped. Real salts are a few
  dozen bytes.

## Windows: the private server closes with the game

Single-player, campaign and benchmark games run on a private `takserver` that the game
starts. On Windows, if the game was killed or crashed, that server kept running in the
background, holding its network port and files; it could even stop an update from
replacing `takserver.exe`. The game now starts it inside a Windows job that ends it as
soon as the game ends, however it ends. The server also writes its log to the same
place as the game's (a console, or a redirected file), as it does on Linux and macOS.

## Tests

The full test run for this release found six test problems, all in the tests rather
than the game, and all fixed:

- Sixteen Legion baseline checks were registered on Windows, where their checker
  cannot run.
- `server_spectators` could send its start before the second player's ready reached
  the server, and then wait forever (seen on Windows).
- `map_cache_readonly` could not see the server's log on Windows, and its Release
  variant used the real Windows profile; that half is now skipped for Release
  builds on Windows and macOS.
- `replaybar` failed under Clang, which removed the test's unused allocations.
- `disconnect_client` checked its replay in a fresh profile, which failed with a data
  folder that has game mods.
- `streaming_pacing`, a real-time encoding test, now runs only in optimized builds.

## Known limits

- All of 0.9.17's limits carry over (see
  [release-0.9.17-notes.md](release-0.9.17-notes.md#known-limits)).

Validation: [release-0.9.18-validation.md](release-0.9.18-validation.md).
