# 0.8.6 release validation

Protocol **281** (0.8.5: 278), replay format **13**, generator version **9**,
campaign payload **4**, database schema **9** (replay, generator, campaign and
schema unchanged since 0.8.5).
<!-- LEAD: re-check kNetVersion right before tagging.
     Version 0.8.6 and protocol 281 are already written in: CMakeLists.txt
     (project VERSION); README.md (badge, title alt text, gallery footnote,
     New in 0.8.6, random maps x2, multiplayer, overrides); docs/user-guide.md
     (intro, Versions, Replays, public server, old replays);
     docs/public-server.md; docs/random-map-generation.md (header line);
     docs/multiplayer-design.md (protocol summary, 279-281 replay history, the
     required-protocol line); these notes and the release notes.
     docs/img/README.md belongs to the screenshots branch. If kNetVersion moves
     past 281, update every one of them. -->

<!-- LEAD: tag line, e.g. "Tagged `v0.8.6` at `<sha>` (annotated tag object `<obj>`) and published on <date> UTC." -->
**PLACEHOLDER (lead): tag and publication line.**

## Scope and compatibility

0.8.6 follows 0.8.5 (protocol 278). After the 0.8.5 validation commit
`216feeed`, main reached `575a70d2` in 27 commits: 20 changes and 7 merges of
the task branches. `git diff --stat v0.8.5..575a70d2 -- src` touches 16 files:
`src/client/` (11), `src/sim/` (4) and `src/net/` (1). Nothing under `src/ai/`,
`src/server/`, `src/campaign/`, `src/tnt/`, `src/cartographer/`,
`src/terrain/`, `src/cob/`, `src/crt/` or `src/util/` changed.

<!-- LEAD: confirm that after 575a70d2 the release preparation added only the
     screenshots, the documentation and the CMake version bump (or list the
     prep fixes that landed). -->
**PLACEHOLDER (lead): what landed after `575a70d2`.**

The changes:

- **279** Legion at ~100 bodies, round 1 (landing `48c5b05c`; `1848af7c`,
  `5d697081` one admission per narrow passage, `659ce752` head-on meets near
  goals, `906339c1` friendly step-aside and brisk detours for departures,
  `7827f6e8`, `8e60cc60` attack-ring upkeep, press-in and ringcross; cost
  baseline `4fe78bdb`). Legion only.
- **280** Legion at ~100 bodies, round 2 (landing `aacb1f2c`; `0c820175`,
  `53863b04` sub-cell presses and ring bends, `2d29aecd` passage lanes through
  wide gaps, `96f301f6` flow-around at travel speed). Legion only.
- **281** blocked-shot repositioning, all pathfinding modes (`c74c7f4e`,
  `94fb3b17`, protocol bump `575a70d2`): `World::noteShotImpact`,
  `shotBlockedHere`, `repositionForShot`, `Unit::searchFailed`; the state is
  folded into `World::stateHash()` only once a blocked impact occurs. A
  documented deviation from retail (`docs/retail-engine.md`).
- Client only: voice chat TALK MODE / ACTIVATION LEVEL (`aab3a14f`,
  `voice::VoiceGate`, settings `voiceActivation`, `voiceActivationLevel`);
  enemy-hover cursor by retail selector `0x4dd780` (`f72fb389`, with
  `cf35db0c` restoring Attack orders for every selected unit).

**Retail is expected to be bit-identical to 0.8.5 except where a shot is
blocked** (279 and 280 are Legion only). `--mpai` Inner Circle seed 1 on
`575a70d2`, from the landing commit: Retail 60 s `b353410003e98dd5`, 180 s
`fe47c6bf443c5d37`; Legion 60 s `0825aeb9e8139203`, 180 s `1e87f71790f15023`,
all equal to 0.8.5.
<!-- LEAD: confirm the four --mpai hashes on the final main (Local preparation checks). -->

**Compatibility.** `kNetVersion` is **281** in `src/net/protocol.h` at
`575a70d2`, against 278 at `v0.8.5`. `kReplayFormat` is **13** in
`src/net/replayhdr.h`, the map generator's `formatVer` **9**
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` 4
(`src/net/crusades.h`) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`), all unchanged.

- **Multiplayer.** The server's Hello check rejects a client whose protocol
  differs from `kNetVersion`, so **0.8.6 cannot play with 0.8.5 or earlier in
  either direction**. The server and all players must update together.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  protocol equals `kNetVersion`, so 0.8.6 does not play 0.8.5 or older replays
  and those builds do not play 0.8.6 replays, although the file format is still
  13. `tools/replay_test.cpp` pins `kNetVersion == 281` and refuses 280, 279
  and 278.
- **Maps and settings.** Saved `.kmp` maps and generator-8/9 recipes carry
  over. A 0.8.5 settings file reads the same in 0.8.6; new keys
  `voiceActivation` (default off, Push to Talk) and `voiceActivationLevel`
  (default 15). 0.8.5 drops them when it saves.

## Local preparation checks

<!-- LEAD: paste the local checks run on the final source (worktree
     ~/tak-tmp/wt/release-086-local): toolchains; Release / Debug / -O2 / Clang /
     ASan builds and warning counts; the full CTest table per tree (test count
     against 0.8.5's 678 plus the new blocked_shot, legion_blocked_shot,
     cursor_enemy and 15 legion_* tests); unoptimized Legion test times against
     their limits; the settings-leak guard; check-detmath and check-determinism;
     the python research suite; the retail archive verifier; the four --mpai
     hashes; the Legion gate. -->
**PLACEHOLDER (lead): local preparation checks.**

## Remote multiplayer sweep

<!-- LEAD: paste the 37-scenario remote sweep (tools/desync-hunt-remote.sh) for
     Retail and Legion: date and commit, build id, referee hosts (tak.pgnet.us is
     NEVER the public takserver; isolated binaries and ports), per-mode timings,
     mismatches, the load-sensitive cases (Retail h-stress/h-lake, 4h-gods) and
     h-cramped. Note whether any scenario exercises blocked shots. -->
**PLACEHOLDER (lead): remote multiplayer sweep.**

**Mixed versions.** None can be played: 0.8.6 speaks protocol 281, and the
server's Hello rejects any client at another protocol, so 0.8.6 is neither
multiplayer- nor replay-compatible with 0.8.5.

## CI and packages

<!-- LEAD: main CI on the final source (Determinism, Linux packages, Windows,
     macOS run ids and any reruns / known flakes: macOS x64 stream_test, macOS
     "Upload CI artifact" ENOTFOUND, Ubuntu 22.04 crusades_client_network_test);
     tag CI run ids (determinism dispatched with gh workflow run determinism.yml
     --ref v0.8.6); the 29 assets checked against gh release view digests; build
     ids "0.8.6 (build v0.8.6)" on Linux, Windows 11 (oden-win) and the MacBook;
     Authenticode Valid on the Windows files; both DMGs and apps notarized and
     stapled; audio-input entitlement and NSHighResolutionCapable in every macOS
     app; no libsecret/glib NEEDED in the Linux takclient; Opus static and
     OPUS-LICENSE in every package. -->
**PLACEHOLDER (lead): CI and packages.**

## Screenshots

<!-- LEAD: summarise the recaptures from task-release-086-shots: which gallery
     and guide pictures were retaken (title.jpg with VERSION 0.8.6 at least),
     which were kept and from which build, any new picture (for example the
     Voice Chat page with TALK MODE and ACTIVATION LEVEL, a blocked-shot scene,
     the attack cursor), and that captions were checked against the images.
     Captions that depend on the new shots: README title alt text ("version
     0.8.6") and gallery footnote ("0.7.25 to 0.8.6 builds"); the user guide's
     voice-chat.jpg alt text (already describes the recaptured page: Talk mode
     Push to Talk, Activation level 15 greyed out); docs/img/README.md (header, kept-from
     lists and every "Rendered by the 0.8.x build" cell). -->
**PLACEHOLDER (lead): screenshots.**

## Limits

- No game mixing 0.8.5 and 0.8.6 builds was played, and none can be (protocol
  281 against 278, the Hello gate and the replay loader).
- **Blocked shots:** a stationary auto-targeting shooter keeps refusing a
  once-blocked target even after it moves into the open (an attack order or a
  move clears it); the clear-shot search covers about 12 cells. Retail has no
  such behaviour at all; this is a documented deviation.
- **Legion passage admission is still a lottery at ~100 bodies:** over the
  wide offsets, `taperbridge-100` gridlocks at offset +1 (a gate offset: 19 of
  100 arrive) and `bridges-100` at another; `legion_passage_admission` and
  `legion_passage_taper` hold their offsets. See docs/legion-pathfinding.md,
  Known weaknesses.
- Legion attack rings along walls fill more thinly than Retail's, a target
  walled in on every side is ringed but never fired on, and the faster
  flow-around raises lane swaps where streams cross after a turn
  (`mixed2:gate.mid.crossings` 13 -> 38, Retail 0).
- The voice-activation gate and the enemy-hover cursor are client-only and are
  covered by `voice_test` and `cursor_enemy`; neither is visible to the remote
  sweep.
- Carried over: the Legion 10,000 program still does not meet its own success
  criterion for congested workloads, and many-10000, scatter-10000 and
  mixed-10000 still run over the 33.3 ms tick budget in part of their ticks;
  mixed-10000's flyers hover over a slow ground army. Retail `h-cramped` ends
  early because the idle seat starts with only its Monarch. The macOS Retina
  UI Scale is migrated once.
<!-- LEAD: add any limit found by the local checks or the remote sweep. -->
