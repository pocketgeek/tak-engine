# 0.8.5 release validation

Protocol **278** (0.8.4: 277), replay format **13**, generator version **9**
(0.8.4: 8), campaign payload **4**, database schema **9** (replay, campaign and
schema unchanged since 0.8.4).
<!-- LEAD: re-check kNetVersion right before tagging.
     Version 0.8.5 and protocol 278 are already written in: CMakeLists.txt
     (project VERSION); README.md (badge, title alt text, gallery footnote,
     New in 0.8.5, random maps x2, multiplayer, overrides); docs/user-guide.md
     (intro, Versions, Replays, public server, old replays);
     docs/public-server.md; docs/random-map-generation.md (generator 9);
     docs/multiplayer-design.md (protocol summary, 278 replay history, the
     required-protocol line); docs/img/README.md (title.jpg row only); these
     notes and the release notes. If kNetVersion moves past 278, update every
     one of them. -->

<!-- LEAD: tag line, e.g. "Tagged `v0.8.5` at `<sha>` (annotated tag object
     `<obj>`) and published on <date> UTC." -->

## Scope and compatibility

0.8.5 follows 0.8.4 (protocol 277). After the 0.8.4 validation commit
`8b8618db`, main reached `8788c4fa` in 15 commits: 10 changes and 5 merges of
the task branches. `git diff --stat v0.8.4..8788c4fa -- src` touches 29 files:
`src/client/` (18), `src/cartographer/` (5), `src/tnt/` (3), `src/sim/` (2)
and `src/net/` (1). Nothing under `src/ai/`, `src/server/`, `src/campaign/`,
`src/terrain/`, `src/cob/`, `src/crt/` or `src/util/` changed.
<!-- LEAD: update the commit count and file counts if anything lands after
     8788c4fa (release fixes, docs, screenshots). -->

The changes:

- **278** random-map recipe version 9, Scattered Mana (`b1d5749f`; lobby
  layout `8984d094`, `8788c4fa`). The level rides in the `~gen1~` map id as one
  byte before the title; a 277 peer would refuse a v9 id. Version-8 ids decode
  as None and regenerate byte-identically (`mapgen_test`'s asset-free golden
  `0x6bf977a42891c638` unchanged).
- Death and unit sounds on retail's view plane, carried and cloaked other-owner
  units silent (`296ebe0f`, `2c557b47`). The simulation records the position
  and hidden state with each `ScriptSound` (`src/sim/sim.{h,cpp}`); that
  record is presentation only and not folded into `World::stateHash()`.
- Client only: native Retina resolution on macOS (`b0f823f3`); world effects
  and projectiles hidden under Tactical Dots (`5cd02b1d`, `e7f6cdda`); Screen
  Shake option, 50% scales and new graphics defaults (`025bedfe`); no
  queued-build ghost over a site under construction (`2a84a023`).

**Retail and Legion are expected to be bit-identical to 0.8.4** on authored
maps and on generated maps with Scattered Mana at None.
<!-- LEAD: confirm with the --mpai Inner Circle seed 1 runs on the final main and
     write the four hashes here (0.8.4: Retail 300 s a3d953803a064191, 60 s
     b353410003e98dd5; Legion 300 s ce5c4ba7562e53b9, 60 s 0825aeb9e8139203). -->

**Compatibility.** `kNetVersion` is **278** in `src/net/protocol.h` at
`8788c4fa`, against 277 at `v0.8.4`. `kReplayFormat` is **13** in
`src/net/replayhdr.h` (unchanged); the map generator's `formatVer` is **9**
(`src/tnt/mapgen.h`, 8 at `v0.8.4`), the campaign payload's `kVersion` 4
(`src/net/crusades.h`, unchanged) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`, unchanged).

- **Multiplayer.** The server's Hello check rejects a client whose protocol
  differs from `kNetVersion`, so **0.8.5 cannot play with 0.8.4 or earlier in
  either direction**. The server and all players must update together.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  protocol equals `kNetVersion`, so 0.8.5 does not play 0.8.4 or older replays
  and those builds do not play 0.8.5 replays, although the file format is still
  13. `tools/replay_test.cpp` pins `kNetVersion == 278` and refuses 277.
- **Maps and settings.** Saved `.kmp` maps and generator-8 recipes carry over.
  A 0.8.4 settings file reads the same in 0.8.5 and keeps its saved values;
  the new defaults apply only to keys a file does not have (and a file with no
  texture-filter key at all now resolves to Everything instead of Off). New key:
  `screenShake`. 0.8.4 drops it when it saves, and clamps a scale below 75% to
  75%.

## Local preparation checks

<!-- PLACEHOLDER (LEAD / validation agents): builds (Release, Debug, -O2 -g,
     Clang, ASan), ctest results per build, tools/check-detmath.sh,
     tools/check-determinism.sh, the python research suite, the retail archive
     verifier, the --mpai hashes above, any test fixes needed. -->

*To be written.*

## Remote multiplayer sweep

<!-- PLACEHOLDER (LEAD / validation agents): tools/desync-hunt-remote.sh, the
     37-scenario table for Retail and Legion, binaries/build ids, referee hosts,
     mismatches (expected 0), reference ticks (0.8.4: Retail h-stress tick
     5,240 1fe1eb4e3754ca93; Retail h-cramped ends at 1,307). Never touch the
     public takserver. Consider one Scattered Mana generated-map game on the
     remote referees, since 278 is the only simulation-facing change. -->

*To be written.*

## CI and packages

<!-- PLACEHOLDER (LEAD): main CI runs on the final source; tag CI run ids
     (Determinism dispatched with gh workflow run determinism.yml --ref v0.8.5,
     Linux, Windows, macOS incl. Sign + notarize); asset count and digests;
     build ids "build v0.8.5"; Authenticode on oden-win; DMG/app notarization
     and stapling on the MacBook; no libsecret/GLib NEEDED on Linux takclient;
     Opus static and OPUS-LICENSE present. New for 0.8.5: a packaged macOS
     takclient on a Retina display logs "dpi: native Retina resolution;
     backing scale 2.00". -->

*To be written.*

## Screenshots

<!-- PLACEHOLDER (LEAD / screenshot agent, branch task-release-085-shots):
     which pictures were recaptured and with which build. Captions written for
     new captures by the docs branch, which must match the new pictures:
     - title.jpg (VERSION 0.8.5): README alt text, gallery footnote
       ("0.7.25 to 0.8.5 builds"), docs/img/README.md row;
     - guide/options-graphics.jpg: user-guide alt text and caption now describe
       the 0.8.5 defaults (Texture Filtering Everything, Smooth GUI Art and
       Smooth Movies on, Tactical Dots on at 0%, Screen Shake off) and no
       longer say Tactical Dots Zoom is greyed out;
     - guide/tactical-dots.jpg: user-guide caption now says effects are drawn
       only without dots and no longer names the 20% zoom or "the blue spell
       effect drawn either way".
     Possibly affected, captions unchanged: cartographer/random-generator.jpg
     (the dialog has a new SCATTERED MANA row; its alt text lists the fields),
     and the lobby generator panel if a picture of it exists (Scattered Mana
     slider, SIZE and PLAYERS on one row). docs/img/README.md still describes
     the 0.8.4 capture profiles (Smooth GUI Art off, Texture Filtering Off). -->

*To be written.*

## Limits

- No game mixing 0.8.4 and 0.8.5 builds was played, and none can be (protocol
  278 against 277, the Hello gate and the replay loader).
- The macOS Retina change was tried on one MacBook, including moving the
  window between a 2× and a 1× display. UI Scale is migrated once, so on a display of the other
  scale the interface looks larger or smaller until UI Scale is changed.
- Local visual inspection covers Linux OpenGL only; Windows and macOS packages
  are checked from CI and by signature, notarization and build-id checks.
<!-- LEAD: carry over the 0.8.4 limits that still apply (Legion 10,000
     congestion and tick overruns, mixed-10000 under 277, Retail h-cramped tick
     1,307) and anything new from the sweeps. -->
