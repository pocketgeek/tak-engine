# 0.8.8 release validation

Protocol **288** (0.8.7: 284), replay format **14** (0.8.7: 13), generator
version **9**, campaign payload **4**, database schema **9** (generator,
campaign and schema unchanged since 0.8.7).
<!-- LEAD: re-check kNetVersion right before tagging.
     Version 0.8.8 and protocol 288 are already written in: CMakeLists.txt
     (project VERSION); README.md (badge, New in 0.8.8, random maps x2,
     multiplayer incl. replay format 14, mods); docs/user-guide.md (intro incl.
     replay format 14, Versions, Replays, public server, old replays);
     docs/public-server.md; docs/random-map-generation.md (header line);
     docs/multiplayer-design.md (protocol summary "0.8.8 ships protocol 288",
     285-288 history, the required-protocol line); these notes and the release
     notes.
     NOT bumped (they describe pictures, and move with the recapture): the
     README title alt text ("version 0.8.7"), the gallery footnote ("0.7.25 to
     0.8.7 builds") and docs/img/README.md. If kNetVersion moves past 288,
     update every one of them. -->

Tagged `v0.8.8` at `TODO-LEAD` (annotated tag object `TODO-LEAD`) and published on TODO-LEAD.

## Scope and compatibility

0.8.8 follows 0.8.7 (protocol 284). After the 0.8.7 validation commit
`261112f6`, main reached `0e78b3a6` in 38 commits, all linear (the task
branches were cherry-picked; no merges). `git diff --stat 261112f6..0e78b3a6 --
src` touches 73 files: `src/sim/` (9: `legion.cpp`, `legion.h`,
`matchsetup.cpp`, `matchsetup.h`, `powercodes.h` (new), `retailrng.h`,
`scenarioinfo.cpp`, `sim.cpp`, `sim.h`), `src/net/` (10, among them
`protocol.h`, `replayhdr.h`, `matchconfig.*` and `modpackage.*`), `src/server/`
(3: `server.cpp`, `commands.h`, `crusades/replayfiles.cpp`), `src/hpi/` (2),
`src/util/` (2: `png.*`), `src/cartographer/` (2), `src/vendor/stb/` (2, new)
and `src/client/` (43). Nothing under `src/ai/`, `src/campaign/`, `src/tnt/`,
`src/terrain/`, `src/cob/` or `src/crt/` changed.

<!-- LEAD: if anything lands after 0e78b3a6 (fixes, screenshots, these docs),
     list it here and say whether the checks below still stand for it. -->
After `0e78b3a6` the preparation added TODO-LEAD (screenshots, the 0.8.8
documentation with the CMake version bump to 0.8.8, this report).

The changes:

- **285** Legion at ~100 bodies, round 5 (`a166a6b3` press into the shadow from
  just outside reach, `572b98ad` hold in the shadow only a body that pressed in,
  `15fc9543` late rings for fight-move columns, the reach edge and the stop
  origin, `cc0e69b5` melee presses test contact; bump `4442f658`; scenario
  baseline `a371ae1f`, `cd93d4aa` `legion_meleepress` only with
  `TAK_TEST_DATA`). Legion only.
- **286** Legion at ~100 bodies, round 6, gatehouse corridors (`71854a51`,
  `4984bf18` walk on past the shot; `0879cc45`, `bcbec8c0` deep rings fill back
  to front; bump `70000b3e`; scenario baseline `cf4cf657`). Legion only.
- **287** mods replace overrides (`10264a1a` `mods/cosmetic` and `mods/game`,
  `98694a77` the bundled `click.hpi` moves to `mods/cosmetic`, `e72c5a79` docs,
  `f9055101` cosmetic layers can never stop or desync a game, bump `28884f2b`,
  `69d8ccc0` libc++ test fix). The simulation is 286's.
- Client only, no protocol change: the in-game chat box (`a5868648`,
  `5b1cae90`), Noto Sans Bold in the chat box (`455120e3`, `fe69ec08`
  `CHAT-FONT-LICENSE.txt` packaged everywhere, `34995876`).
- **288** power codes (`166f17f8` the room option, `Cmd::PowerCode` and the
  seven simulation codes; `39752ef8` the client, +Radar and MakePoster;
  `8785b526`, `335edc47`, `24a0396f` review fixes; `adc98de9`, `2c0aeb29` docs;
  bump `9fa322b5`). With power codes off the simulation is 287's.
- Client only: the UI font (`77262226` one text API, `816d6bf4` UI FONT
  MODERN/CLASSIC, `80b9de87` measured widths, `dd0cb7e0` docs, `ff13e8c6`,
  `0e78b3a6` review fixes).

**Retail is expected to be bit-identical to 0.8.7** (285 and 286 are Legion
only; 287 and 288 change no simulation state with power codes off). `--mpai`
Inner Circle seed 1 from the landing commits: Retail 60 s `b353410003e98dd5`
and Legion 60 s `0825aeb9e8139203` (288's commit body), equal to 0.8.7.
TODO-LEAD: the 300 s hashes, and Linux, Windows and macOS.

**Compatibility.** `kNetVersion` is **288** in `src/net/protocol.h` at
`0e78b3a6`, against 284 at `v0.8.7`. `kReplayFormat` is **14** in
`src/net/replayhdr.h` (13 at `v0.8.7`). The map generator's `formatVer` **9**
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` 4
(`src/net/crusades.h`) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`) are unchanged.

- **Multiplayer.** The server's Hello check rejects a client whose protocol
  differs from `kNetVersion`, so **0.8.8 cannot play with 0.8.7 or earlier in
  either direction**. The server and all players must update together.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  protocol equals `kNetVersion`, so 0.8.8 does not play 0.8.7 or older replays
  and those builds do not play 0.8.8 replays. 0.8.8 writes replay format 14
  (the POWER CODES byte after the MAP SCENARIO byte; a format-13 file reads it
  as 0). `tools/replay_test.cpp` pins `kNetVersion == 288` and
  `kReplayFormat == 14` and refuses 287, 286 and 285.
- **Maps and settings.** Saved `.kmp` maps and generator-8/9 recipes carry
  over. A 0.8.7 settings file reads in 0.8.8; the override keys
  (`gameCreate.overrides`, `overrides.hostPack`, `overrides.cosmeticPack`) are
  ignored and not written back. New keys: `uiFont`, `chatScale`, `chatAlign`,
  `gameCreate.powerCodes`, `gameCreate.gameMods`, `mods.gamePack`,
  `mods.cosmeticPack`, and the `view.poster` hotkey.
- **Mods.** `overrides/` is no longer read and is not moved; the mod cache is
  `ModCache/*.takmods` (was `OverrideCache/*.takoverrides`).

## Local preparation checks

<!-- LEAD: fill from the local-checks run (0.8.7 shape: clean detached
     worktree, toolchains, cores 4-23, owned retail install as --data and
     TAK_TEST_DATA, ASAN_OPTIONS=detect_leaks=0 for the ASan tree). -->

All checks ran from a clean detached worktree (`TODO-LEAD`) at `main` `TODO-LEAD`
(protocol **288**, replay format **14**, generator recipe **9**, campaign payload 4,
database schema 9). Toolchains: TODO-LEAD.

**Result: TODO-LEAD.**

- **Multiplayer and replays.** `kNetVersion` is **288** in `src/net/protocol.h` (284 in
  0.8.7). Replay format 14 (13 in 0.8.7); the replay loader requires protocol ==
  `kNetVersion`.
- **Builds.** Release, Debug, optimized Debug (`-O2 -g`), Clang Debug and AddressSanitizer
  all-target builds: TODO-LEAD (steps, warnings; new warnings since 0.8.7; 0.8.7: 873
  Release / 874 other steps, 935 Release warning lines).
- **Full CTest.** TODO-LEAD registered tests (0.8.7: 719 Debug / 681 Release). Added since
  0.8.7: `legion_ringshadowpress`, `legion_ringshadowmixed`, `legion_fightring`,
  `legion_ringedge`, `legion_meleepress` (only with `TAK_TEST_DATA`),
  `legion_gatewalkon`, `legion_gateyard`, `legion_walkononward`, `legion_ringdeep`,
  `legion_ringdeepchurn`, `power_codes`, `server_power_codes`, `poster`, `chatbox`,
  `uifont`. Renamed: `override_packs` -> `mod_packs`, `override_settings` ->
  `mod_settings`, `override_transfer` -> `mod_transfer` (TODO-LEAD: confirm against
  `ctest -N`).

  | Tree | Selection | Result | Time | Failures |
  | --- | --- | --- | ---: | --- |
  | Release | `-LE nightly` | TODO | TODO | TODO |
  | Release | `-L nightly` | TODO | TODO | TODO |
  | Optimized Debug | `-LE nightly` | TODO | TODO | TODO |
  | Optimized Debug | `-L nightly` | TODO | TODO | TODO |
  | Debug (`-O0`) | `-LE nightly` | TODO | TODO | TODO |
  | Clang Debug | `-LE nightly` | TODO | TODO | TODO |
  | ASan | `-LE nightly` | TODO | TODO | TODO |

  - **The settings-leak guard.** TODO-LEAD (`pref_guard_setup`/`pref_guard_check`; the real
    profile's `settings.ini` SHA-256 and mtime before and after).
  - Known load flakes to watch: `voice` (optimized Debug), `mod_transfer` (was
    `override_transfer`, Debug), `cartographer_recovery` (under `-j20`).
- **`--mpai` golden hashes.** Optimized Debug build, Inner Circle, `takserver --local
  --no-auth --seed 1`, an empty `XDG_DATA_HOME`, and `TAK_LEGION=1` for Legion.

  | Mode | Duration | TODO-LEAD (288) | 0.8.7 (284) |
  | --- | ---: | --- | --- |
  | Legion | 300 s | TODO | `ce5c4ba7562e53b9` |
  | Retail | 300 s | TODO | `a3d953803a064191` |
  | Legion | 60 s | TODO | `0825aeb9e8139203` |
  | Retail | 60 s | TODO | `b353410003e98dd5` |

- **Determinism.** `tools/check-determinism.sh`: TODO-LEAD (0.8.7: GCC and Clang x86-64
  O0/O2/O3 agree on `dcef618cd2e4d558`). `tools/check-detmath.sh`: TODO-LEAD.
- **Legion scenario gate.** TODO-LEAD (`legion_scenario --mode both --json --offsets gate`
  over every `tools/scenarios/*.scn`, then `legion_check.py check --baseline
  tools/scenarios/baseline.json`). The baseline was retaken for rounds 5 (`a371ae1f`) and 6
  (`cf4cf657`, "legion_check PASS, 36,522 keys, 0 failed") and now covers the five
  fixtures 0.8.7 left without entries, so the gate is expected to pass with 0 failed keys.
- **Mods.** TODO-LEAD (`mod_packs`, `mod_settings`, `mod_transfer`; an in-client check of a
  ticked cosmetic pack, an always-on `mods/cosmetic/*.hpi` and its ALWAYS ON line, GAME
  MODS in a multiplayer room, and the `overrides/` notice, if done).
- **Power codes.** TODO-LEAD (`power_codes`, `server_power_codes`, `poster`; an in-client
  check of the nine codes and a poster with `TAK_POWER_CODES=1 TAK_POWER_SCRIPT`, if done).
- **Chat box and UI font.** TODO-LEAD (`chatbox`, `uifont`; an in-client look at CHAT
  SCALE 50%/300%, CHAT POSITION LEFT/CENTERED, UI FONT MODERN/CLASSIC, if done).
- **Research suite and retail archive verifier.** TODO-LEAD (0.8.7: 165 + 6 tests; the
  replay-header tests now expect format 14).
- **Syntax checks.** TODO-LEAD (Python compile, `bash -n`).

## Remote multiplayer sweep

<!-- LEAD: fill from tools/desync-hunt-remote.sh, both pathfinding modes;
     referees on tak.pgnet.us + vpn3.pgnet.us with isolated binaries and ports
     (never the public takserver). -->

The full 37-scenario table for both pathfinding modes, Retail and Legion, on
**TODO-LEAD** (UTC), on `main` at `TODO-LEAD` (protocol **288**, replay format 14,
build id `TODO-LEAD`):

- **Retail:** TODO-LEAD.
- **Legion:** TODO-LEAD (`TAK_WALL_TIMEOUT=3600` from the start, as since 0.8.1).

| Mode | Completed scenarios | Completed seats | Client-pair checkpoints | Client-referee checkpoints |
| --- | ---: | ---: | ---: | ---: |
| Retail | TODO/37 | TODO/52 | TODO | TODO |
| Legion | TODO/37 | TODO/52 | TODO | TODO |

Each run's `--validate` gates (build identity, planted desync, mod transfer):
TODO-LEAD. The harness changed with the mods rework: the transfer gate now prints
"verified game mods transferred and incorrect loaded gameplay rejected", and the case
`h-overrides-full` is now `h-mods-all` (`--mods all`), so its "vs 0.8.7" row compares
across the rename. The script's SHA-256 is no longer 0.8.5's
(`0ec213448db4f15d...`); at `0e78b3a6` it is
`df09e28844be1f399f9466a62e19c9cdf53298e5fdb9eb5dcefb5fca830ad56e`.

### Reference ticks and hashes (0.8.8 candidate)

TODO-LEAD: the per-case table (Retail and Legion end, tick, hash, units, vs 0.8.7). Retail
is expected to match 0.8.7 case for case except the six `TAK_AUTOPLAY` cases and the
load-sensitive `h-lake`, `h-stress`, `2h-stress`, `2h-gods`, `4h-gods`.

### Comparison with 0.8.7

TODO-LEAD. Legion is expected to move in the cases with attack rings along walls or
through gates (rounds 5 and 6).

## CI and packages

TODO-LEAD: main CI on the final source (Determinism, macOS, Windows, Linux packages);
tag CI run ids (determinism dispatched with `gh workflow run determinism.yml --ref
v0.8.8`); reruns and which known flake; asset count (0.8.7: 29), digests and sizes; build
ids `v0.8.8` in every package; Windows Authenticode (0.8.7: 9 files Valid on oden-win);
macOS notarization and stapling of both DMGs and apps; Opus static and
`OPUS-LICENSE.txt`; **new: `CHAT-FONT-LICENSE.txt` in every Debian, RPM, Arch and
Slackware package, both Linux debug tarballs, all release and debug ZIPs (the game app's
Resources on macOS) and the Windows installer**; microphone entitlement;
`NSHighResolutionCapable`; no libsecret/GLib `NEEDED` on Linux; publication time.

## Screenshots

TODO-LEAD: which gallery and guide pictures were recaptured on the 0.8.8 candidate and how
(0.8.7 capture settings), which were kept, and the captions checked. The UI font changes
the lettering of every interface screen, so the candidates are `title.jpg` (VERSION 0.8.8),
`lobby.jpg`, `lobby-scenario.jpg`, `room.jpg` (mods column: COSMETIC MODS / GAME MODS
instead of override packs; POWER CODES row), `diplomacy.jpg`, `campaign.jpg`, and
`guide/{random-map-lobby,hud-annotated,scorecard,side-*,selection-strip,options-graphics,voice-chat,campaign-briefing,replay,benchmark}.jpg`,
plus the scenes retaken each release; a chat-box picture is new if one is wanted. Then bump
the README title alt text, the gallery footnote and `docs/img/README.md` to 0.8.8 (the
`room.jpg` caption there still names "two example override packs").

## Limits

- No game mixing 0.8.7 and 0.8.8 builds was played, and none can be (protocol
  288 against 284, the Hello gate and the replay loader).
- **Legion units still stand and wait more than Retail's**, though they arrive
  sooner (a user ruling: cosmetic).
- **Legion attackers through a narrow gate deal more damage than Retail's**
  (gate sums: `attackring-gatehouse-40` 11,719 against 7,172,
  `attackring-gateyard-36` 8,678 against 5,655, `attackring-gatehouse2-48`
  12,183 against 10,404, `attackring-deadcorridor-30` 5,077 against 2,262), and
  Legion work on the gatehouse layouts rose (gatehouse-40 whole run 1.72M ->
  6.49M at the median).
- **Power codes are lockstep**: every code that changes the game is a
  sequenced command, so its effect is in every player's simulation and in the
  replay; retail applied codes only on the typer's machine.
- **FULL VISION does not switch LOS off at start** (retail seeds it off);
  +LOS / +NowISee do.
- **UI FONT CLASSIC is close to but not pixel-identical with 0.8.7** (a space
  advances 4 px everywhere, and layouts now measure text widths); accepted.
- Carried over: the Legion bridge lottery (`taperbridge-100`,
  `taperbridge-small`); the Legion 10,000 program still does not meet its own
  success criterion for congested workloads, and many-10000, scatter-10000 and
  mixed-10000 still run over the 33.3 ms tick budget in part of their ticks;
  mixed-10000's flyers hover over a slow ground army. Retail `h-cramped` ends
  early because the idle seat starts with only its Monarch. The macOS Retina
  UI Scale is migrated once. A stationary auto-targeting shooter keeps refusing
  a once-blocked target.
- TODO-LEAD: any flake or failure seen in this release's runs.
