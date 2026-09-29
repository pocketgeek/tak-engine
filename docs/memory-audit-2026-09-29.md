# Memory and resource audit — 2026-09-29

This audit follows the 0.7.11 release. Its fixes ship in **0.7.12**;
the earlier 0.7.11 packages do not contain them.
No simulation rules, protocol, pathfinding, or shipped dependencies changed.

## Findings and fixes

| Area | Finding | Change |
| --- | --- | --- |
| Loading screens/fonts | A process-static font kept renderer-owned glyph textures beyond the screen/renderer lifetime. Font assignment also had no ownership cleanup. | Fonts now own their glyphs, support moves rather than shallow copies, and clean up partial construction. Loading-screen fonts belong to the screen. |
| Results screen | The missing-heading-font fallback shallow-copied body glyph pointers, giving two fonts ownership of the same textures. | Draw through a reference to the available font. |
| Model viewer | Its texture map held raw pointers with no texture cleanup. | Own textures individually and destroy the viewer before its SDL renderer. |
| Cartographer | Explicit SDL teardown ran before the stack-owned MapView destructor, leaving it to destroy textures belonging to an already destroyed renderer. | Scope-owned SDL/window/renderer resources enforce the correct destruction order on ordinary and early returns; feature thumbnails are explicitly freed. |
| Terrain map reload | AddressSanitizer caught a heap-use-after-free: the chunk worker was reading a compositor image while reload cleared that image. Old CPU mipmaps also survived map reload. | Cancel queued work, wait for the active composition to finish, and clear its mipmaps before changing the compositor or remounting its VFS. Ordinary camera/cache invalidation remains asynchronous. |
| Streaming | The application-owned Stream retained its last input frame allocation after stopping. This was bounded retention, not an unreachable leak. | Release the buffer when the encoder worker finishes; reject late submissions after cancellation/cleanup. UI stop remains nonblocking. |
| Test fixtures | Production and duration tests leaked heap-allocated Worlds. | Own fixtures with `unique_ptr`; these leaks were in tests, not the game. |

The terrain failure was reproduced by drawing a map and immediately reloading a
different map while the background chunk worker was active. The loading-screen
regression test failed before the fix because tracked glyph textures remained
allocated after the screen closed.

## Verification

- All targets rebuilt in Release, optimized Debug, and Clang AddressSanitizer.
- Release CTest: **95/95**; optimized Debug: **98/98**;
  AddressSanitizer/LeakSanitizer: **94/94**, with no sanitizer errors.
  Counts differ because optional native-reference tests depend on build configuration.
- New screen lifecycle regression covers two SDL renderer lifetimes and three
  iterations each of loading, font replacement, campaign selection, and results
  screens, including the missing-heading-font fallback. Tracked texture count
  and bytes return to the initial baseline after each scope.
- Terrain regression reloads maps immediately after scheduling chunk work,
  then checks that the new map finishes rendering and old textures are released.
- Streaming regressions check that stopped workers release input-buffer capacity.
  Encrypted upload, reconnect, hostname rejection, and key redaction also pass
  under AddressSanitizer using a local test endpoint, without public broadcasting.
- An instrumented full-client harness ran ten game/menu cycles: all five factions
  in both balance modes, with two seconds of simulation and drawing per game.
  It also exercised three map reload/model-viewer cycles. Every scope returned
  tracked texture count and bytes to zero; no sanitizer errors remained.
- The same harness opened each of **85 installed Bink files** twice, decoded the
  first five frames, closed it, and attempted an invalid input. No sanitizer errors.
- Cartographer's screenshot-and-exit path passed under the sanitizers.
- A 16,000-unit, eight-Absurd-AI Crusades match ran 900 ticks under the
  sanitizers, ending with 14,605 live units and 8,419 hit events, without errors.
  Its ending hash `c94d1a56d9c65d5a` matches the release smoke run. Sanitizer
  timings are not performance results.
- Five sequential network rooms exercised separate host/peer/server map roots,
  map mismatch/download, and late spectators, each reaching 300 matching ticks
  (`a5a69304ffacaa64`). A scratch copy of the server added only an orderly test
  loop exit; existing grace-timer overrides were shortened to 100 ms. All five
  rooms dissolved, clients and server exited cleanly, and LeakSanitizer reported
  no leaks. The production server has no orderly signal exit, so merely killing
  it would not have been adequate exit-leak verification.

## Reproduction

```sh
cmake -S . -B build-asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ \
  '-DCMAKE_CXX_FLAGS_DEBUG=-O1 -g -fsanitize=address -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address' \
  -DTAK_TEST_DATA=/path/to/retail
cmake --build build-asan -j8
SDL_SHUTDOWN_DBUS_ON_QUIT=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ASAN_OPTIONS=detect_leaks=1:allow_addr2line=1 \
  ctest --test-dir build-asan --output-on-failure -j4
ASAN_OPTIONS=detect_leaks=1:allow_addr2line=1 \
  python3 tools/check-stream-network.py build-asan/stream_network_test
```

`SDL_SHUTDOWN_DBUS_ON_QUIT=1` is used **only by the test process** to release
SDL's otherwise retained D-Bus globals at exit. It is not enabled in the engine:
other libraries may still use D-Bus. No leak suppressions were used. Initial
reports were separated into this external-library exit retention and the actual
engine/test problems above.

## Scope and remaining limits

Source review covered client screens, renderer/terrain ownership, audio/video,
streaming, network/map transfer, server room teardown and replay buffers, and
simulation/test fixture ownership. Server replay logs intentionally grow during
an active match to support reconnect and replay; caches and reusable vector
capacity are not automatically leaks. A freed allocation also need not cause
immediate process RSS reduction because allocators retain reusable memory.

The sanitizer runtime coverage is Linux with SDL software/dummy rendering and
software encoding. It does not prove every hardware-driver path leak-free on
Windows/macOS, nor cover hours-long soak behavior or every frame of every movie.
Static third-party libraries were not rebuilt with sanitizer instrumentation;
intercepted heap allocations still participate in leak detection. The findings
are concrete fixes and clean tested paths, not a claim that all possible leaks
have been eliminated.

Detailed local logs and exploratory harnesses: `/tmp/tak-memory-*`. Retail data,
sanitizer builds, and exploratory harnesses remain outside Git.
