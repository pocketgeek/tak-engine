# 0.8.11 release validation

Protocol **290** (was 289), replay format **14**, generator version **9**,
campaign payload **4**, database schema **9**.

TODO-LEAD: Tagged `v0.8.11` at `<sha>` (annotated tag object `<obj>`) and published on `<date> <time> UTC`.

## Scope and compatibility

0.8.11 follows 0.8.10 (protocol 289). After the 0.8.10 validation commit
`ca6b493a`, main reached `d86d5904` in 35 commits, all linear (no merges).
`git diff --stat ca6b493a..d86d5904 -- src` touches 41 files: `src/ai/` (`ai.cpp`,
`ai.h`, and the new Classic brain `classicbrain.cpp`, `classicbrain.h`,
`classicdefence.h`, `classicexpand.h`, `classicplacement.h`,
`classicproduction.h`, `classicprofile.cpp`, `classicprofile.h`,
`classicsquads.h`, `classictypes.cpp`, `classictypes.h`, `classicwaves.h`);
`src/client/` (`buildrow.h` (new), `campaignscreen.cpp`, `campaignscreen.h`,
`chatbox.h`, `gameview.h`, `gameview_devharness.cpp`, `gameview_hud.cpp`,
`gameview_lobby.cpp`, `gameview_net.cpp`, `gameview_render.cpp`, `main.cpp`,
`mainmenu.cpp`, `settings.cpp`, `settings.h`, `unitindex.h`); `src/net/`
(`client.cpp`, `client.h`, `protocol.h`); `src/server/` (`roomtick.cpp`,
`server.cpp`, `rosterdump.cpp` and `rosterdump.h` (both new, debug only)); and
`src/sim/` (`matchsetup.cpp`, `matchsetup.h`, `retailplayer.h`, `sim.cpp`,
`sim.h`). Nothing under `src/campaign/`, `src/tnt/`, `src/terrain/`,
`src/cob/`, `src/crt/`, `src/video/`, `src/hpi/` or `src/cartographer/`
changed. TODO-LEAD: list the preparation commits (screenshots, version and
docs, this report) and confirm nothing else landed on main after `d86d5904`.

The changes:

- Classic AI (AI level 6, server only; protocol 290): `dbcda124` (level 6
  CLASSIC in the lobby, `kNetVersion` 290), `c3979d72` (`ClassicBrain` stub),
  `e014b959`, `771d7916` (`replay_test` refuses 289), `3c8eed3b`
  (`check-detmath` covers the brain), `98ba4820` (P1 profile), `5fc840b0` (P2
  type info and priority clock), `b6a75fba` (PrintWeights reproduced),
  `c1f833d4` (P3 squads, scheduler, assignment), `007f8941` (P4a factory
  planner), `7cb28f47` (P4b constructor planner and placement), `86b1811e`,
  `ff3688ff` (static reachability bit), `3d335bb0` (P5 defence, patrols,
  Monarch retreat), `1a9822a3` (P6 waves and target picker), `260e127c` (P7
  expansion and transfers), `3cc16453`, `a55e3a23`, `5acbfc9b`, `9f4df7fa`,
  `aad9104e` (U16: enemy sites being built), `2789d7c5` (P8 acceptance ledger,
  docs), `d86d5904` (mission AI profiles; protocol note). Plan and review:
  `56247c26`, `b69dfe89`, `d4349eea`, `becd6fb9`.
- Campaign: `e89d1f1a` (Opponent AI NORMAL / CLASSIC on the book's left page,
  key A, `campaignAi` setting; the choice rides as an optional byte after
  `CreateGame`'s private flag).
- Build menu: `f76a9ce1` (wrap into rows, `client/buildrow.h`, `buildrow_test`,
  `build_controls`), `3351056b` (tooltip and TRAINING line clamped to the play
  area), `7f21e860` (comment).
- Health bars: `cbf234b6` (blue production bar removed; conjure gauge shows the
  produced unit's HP), `6b5b62ae` (comments).
- Developer tooling: `76951abf` (retail AI comparison harness under `tools/re`,
  debug-only `TAK_ROSTER_DUMP`), `950f5e2c` (harness teardown).

A Classic seat gets exactly a Normal seat's match config, and the existing levels'
code paths are unchanged, so the hashed simulation is 0.8.10's for every game
without a Classic seat. The registry change in `src/sim/sim.cpp` only keeps
variant FBI files in a Classic-only side table that nothing in the sim reads;
`setupMission` additionally hands back each slot's Player line. `--mpai`
Inner Circle seed 1 is unchanged from 0.8.10 at 60 s and 300 s in both modes
(see the local checks). TODO-LEAD: `--mpai` on Windows and macOS (optimized
Debug builds of the tag), or state that it was not run this release.

**Compatibility.** `kNetVersion` is **290** in `src/net/protocol.h` at
`d86d5904` (289 at `v0.8.10`). `kReplayFormat` is **14** in
`src/net/replayhdr.h`, as at `v0.8.10`. The map generator's `formatVer` **9**
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` 4
(`src/net/crusades.h`) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`) are unchanged.

- **Multiplayer.** The server's Hello check rejects a client whose protocol
  differs from `kNetVersion` (or whose gameplay-data hash differs). So **0.8.11
  cannot play with 0.8.10 or earlier in either direction**; the server and
  every player must update together.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  protocol equals `kNetVersion`, so 0.8.11 refuses 0.8.10's protocol-289
  replays (format 14 as well) and 0.8.10 refuses 0.8.11's.
  `tools/replay_test.cpp` pins `kNetVersion == 290` and `kReplayFormat == 14`
  and checks that `supportedReplayProtocol(14, 289)` is refused.
- **Maps, mods and settings.** Saved `.kmp` maps, generator-8/9 recipes,
  `mods/` packs and `ModCache/*.takmods` carry over. One settings key was added
  (`campaignAi`, default `normal`); none was removed.

## Local preparation checks

Testing was reduced for this release, by the user's rule (minimal testing):

- **Full CTest, once, on optimized Debug (`-O2`) at `d86d5904`: 782/782 passed**
  (`100% tests passed, 0 tests failed out of 782`, 1,662 s real). The run had the
  opt-in Classic emulation oracles configured (`-DTAK_CLASSIC_ORACLES=ON` and
  `TAK_CLASSIC_PRINTWEIGHTS`; see the
  [acceptance ledger](classic-ai-acceptance.md)), so the oracle set
  (`classic_ai_assignment` ... `classic_ai_zone_labels`, `classic_ai_printweights`)
  passed inside it, together with the Classic game tests (`classic_ai`,
  `classic_ai_data`, `classic_ai_bases`, `classic_ai_raid`, `classic_ai_u13`,
  `classic_ai_waves`, `classic_ai_duels`, `classic_ai_expansion`), `buildrow`,
  `build_controls` and the 162 `nightly`-labelled tests. Log:
  `~/tak-tmp/scratch/retail-ai/land/ctest.out`.
- No Release, `-O0` Debug, Clang or ASan suite was run, and no repeat runs.
- **`--mpai` golden hashes: all unchanged from 0.8.10.** Inner Circle,
  `takserver --local --no-auth --seed 1`, scratch profile, optimized Debug
  binaries at `d86d5904`, `TAK_LEGION=1` for Legion and `TAK_AI_LEVEL=6` for
  Classic. Every game ended `err=none end=timelimit`; no desync or
  referee-suspect line. Logs: `~/tak-tmp/scratch/retail-ai/land/mpai/`.

  | Mode | Length | Hash (units) | 0.8.10 |
  | --- | --- | --- | --- |
  | Retail | 60 s | `b353410003e98dd5` (4) | `b353410003e98dd5` |
  | Retail | 300 s | `a3d953803a064191` (13) | `a3d953803a064191` |
  | Legion | 60 s | `0825aeb9e8139203` (4) | `0825aeb9e8139203` |
  | Legion | 300 s | `ce5c4ba7562e53b9` (18) | `ce5c4ba7562e53b9` |
  | Classic (Retail) | 60 s | `14849c00bc6e2a19` (4), twice | new level |

- **Campaign Normal byte-identity** (from `e89d1f1a`): `takmission08_dh` 300 s
  `68a4b1588012db76` and `takx11_mt` 240 s `c5e3b08d1f08ba9e`, the same before
  and after the Opponent AI change.
- TODO-LEAD: `tools/check-detmath.sh` and `tools/check-determinism.sh` (golden
  `dcef618cd2e4d558` at 0.8.10), if run; else say they were covered by CI's
  Determinism workflow only.
- TODO-LEAD: final source after the preparation commits (rebuild every target,
  `takclient --version`, `ctest -L quick`).

## Remote multiplayer sweep

**Not run for this release.** TODO-LEAD: confirm the justification: the
existing AI levels and the simulation's hashed state are byte-identical to
0.8.10 (the `--mpai` hashes at 60 s and 300 s, Retail and Legion, are
unchanged above), and the sweep's 37 scenarios seat no Classic AI, so it would
replay 0.8.10's table (`docs/release-0.8.10-validation.md`). The Classic level
itself is covered by its emulation oracles and game tests, not by the sweep.

## CI and packages

TODO-LEAD: main CI on the final source (Windows, Linux packages, macOS,
Determinism), noting reruns and known flakes (GitHub runner hang in "Install
build deps", macOS `hdiutil` busy, mirror flakes).

TODO-LEAD: tag CI (Determinism dispatched on the tag, Linux, Windows, macOS
with Sign + notarize) and the asset checks as in 0.8.10: 29 assets, digests
and sizes, build ids `v0.8.11`, Windows Authenticode (9 files), macOS
notarization and stapling, no new `NEEDED` entries, Opus static, licence
files, no `mods/`/`overrides/`/`click.hpi`, microphone entitlement, Retina
flag, MP4 muxer still present; `docs/release-0.8.11-notes.md` and this report
in the tree at the tag with `kNetVersion` 290.

TODO-LEAD: publication line (`gh release edit v0.8.11 --draft=false --latest`).

## Screenshots

Retaken by the 0.8.11 build (version set to 0.8.11 locally; no capture patch),
committed on the screenshots branch (`task-release-0811-shots`, `f16e61c7`) and
each inspected by eye: `docs/img/title.jpg` (VERSION 0.8.11, same 1440 × 1080
crop), `docs/img/campaign.jpg` (fresh profile, chapter 1, with **Opponent AI:
NORMAL (A)** under the always-on mods note) and the new
`docs/img/guide/build-menu-wrap.jpg` (a Zhon Beast Tamer's nine-icon conjure menu
in two rows, five above four, left of the command panel and mana orb; 1024 × 768
at Build Menu Scale 150%, not scaled). The README title alt text, gallery
footnote ("0.7.25 to 0.8.11 builds"), campaign alt text, the user guide's
Building and Campaigns pictures and `docs/img/README.md` were updated to match.
Not retaken: no game picture with bases shows the removed blue production bar;
the lobby and room pictures show each AI seat's level, not the list of levels,
so CLASSIC would not appear; `guide/chat-box.jpg` crops out its Barracks (its
gauge's small early fill reads the same); `guide/campaign-briefing.jpg` is the
briefing, not the book. Scripts: `~/tak-tmp/scratch/release-0811/shots`.
TODO-LEAD: confirm the screenshots commit's sha after it lands on main.

## Limits

- No game mixing 0.8.11 with 0.8.10 or older was attempted: the protocols
  differ, so the server refuses it.
- All of 0.8.10's limits carry over: Legion units stand and wait more than
  Retail's; Legion attackers through a narrow gate deal more damage than
  Retail's; power codes are lockstep (retail applied them only on the typer's
  machine); FULL VISION does not switch LOS off at start; UI FONT CLASSIC is not
  pixel-identical with 0.8.7; the Legion bridge lottery; the Legion 10,000
  workloads' congestion and tick budget, mixed-10000's hovering flyers; Retail
  `h-cramped` ends early; the macOS Retina UI Scale is migrated once; a
  stationary auto-targeting shooter keeps refusing a once-blocked target; the
  campaign book's chapter numeral touches "APTER"; a server-side upload stall
  longer than 1 s is charged to every seat it holds up; a reconnecting seat
  that hitches while catching up is charged as a hold.
- Classic's accepted limits (from the
  [acceptance ledger](classic-ai-acceptance.md#accepted-limits)): the
  simulation is ours, decisions land a tick later and the brain has its own RNG
  stream, so games differ from retail's even where every rule matches; the
  target picker's unit cap is the lobby's (default 2000), not retail's 150 /
  500; no Classic game test near the unit cap (U8); exact ties in the Monarch's
  retreat read statically; U6, U14 and V8 / V11 leftovers; campaign start
  records inferred; order glue (U1, U13); canbuild menu order, seeding and the
  visible-enemy list follow our engine. Classic is weaker than Normal (loses in
  8 to 18 game-minutes in tests) and a Taros Classic AI often grows slowly.
- Testing was reduced (above): one full `-O2` suite, the `--mpai` set, no
  other trees and no remote sweep.
- TODO-LEAD: flakes seen in CI, if any.
