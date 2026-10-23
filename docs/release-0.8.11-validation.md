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
changed. The preparation commits on top of `d86d5904` are `df0c5eef`
(screenshots), `614ffbaf` (version 0.8.11, notes, docs), `f0198b13` (README
Classic row, this report's Screenshots section) and the commit filling this
report; nothing else landed on main after `d86d5904` (checked with
`git fetch` before landing).

One more change landed after that, before the tag, by the user's decision
("Campaigns should default to classic"): branch `task-campaign-classic-default`
(`c44968dd` code and `settings_test`, `654ba4d4` docs, `bfbb40ee`
`campaign.jpg`, and this note). `Settings::campaignAi` now defaults to Classic
(6); a settings file with no `campaignAi` key (every 0.8.10 profile) or an
unknown value reads Classic, and `normal` still reads Normal. Options DEFAULTS
still leaves it alone. The client sends the setting with every mission room;
the server is unchanged (it still plays a missing or unknown room byte as
Normal), and the debug `--mpmission` harness still sends Normal unless
`TAK_AI_LEVEL` picks. Only `src/client/` changed (`settings.h`,
`settings.cpp`, `mainmenu.cpp`, `campaignscreen.h`, `gameview.h`).

Later, still before the tag, the user's decisions of 2026-10-23 replaced that
choice: campaign opponents are always Classic. The book's Opponent AI line and
A key, `Settings::campaignAi` (an old key is ignored), `TAK_SHOT_CAMPAIGN_AI`
and CreateGame's optional AI byte are gone (protocol stays 290, unreleased),
and the `--mpmission` harness now plays Classic with no settings:
`takmission08_dh` 300 s gives `5c35c83f41cbae5f` (the Classic hash above), and
the Normal mission hashes below no longer apply. The Classic AI's profile file
is looked up in the room's layers (game mods, else the map package, else the
base; a map package carries the map archive's own file). The same branch gave our own
AI levels (Defensive to Absurdly Defensive) built-in build profiles
(`src/ai/builtinprofile.{h,cpp}`, the values of the default profile they had
always read, so they play exactly as before) and stopped them reading
`ai/*.txt`; a game pack's `ai/d.txt`, `ai/e.txt`, `ai/n.txt`, `ai/h.txt`,
`ai/a.txt` or `ai/ad.txt` (in its `ai` folder), else the `.kmp` map's own,
adjusts that one level (cosmetic packs and the install cannot carry them; a root
`n.txt` is not read). Besides `src/ai/`, `src/client/`, `src/net/` and
`src/server/`, it touches `src/hpi/` (`hpi.cpp`, `hpi.h`: the game-mod file list
and the cosmetic filter) and `src/net/mappackage.cpp` (a map package may carry
the map's AI profile). See branch `task-ai-profiles`.

Then, by the user's decisions of 2026-10-23 ("Our files, n_ai.txt and whatever
need to go in the ai directory in a mod or game archive"; "rename classic to
default in the game lobby, so the default.txt naming matches that"), branch
`task-ai-dir`: our levels' game-mod files moved into the pack's `ai/` folder
(a root file is no longer read; at first game mods only, named `*_ai.txt`), and
AI level 6 is shown to players as **DEFAULT**, the DEFAULT AI (the lobby's level
name, the server log's `DEFAULT AI profile` and mission lines, `aitool default`
(which still takes `classic`), the user documents). Its internal name stays
Classic. No AI level is stored by name anywhere (settings, replays and the match
codec carry the number), so nothing persisted changes. Code: `src/ai/`
(`builtinprofile.cpp`, `builtinprofile.h`), `src/hpi/hpi.cpp` (the cosmetic
filter's root-name list is gone; `ai/` was already dropped),
`src/client/gameview_lobby.cpp`, `gameview.h` / `gameview_net.cpp` (the debug
`TAK_AI_LEVELS` per-seat harness levels), `src/server/server.cpp`,
`tools/aitool.cpp`. Protocol stays 290.

Then, same day and branch ("for all of our ai files remove the _ai from the file
name"; "Custom ai files should also be read from kmp map files"; "Game mods should
win over what is in a map file"): the level files are `ai/d.txt`, `ai/ad.txt`,
`ai/e.txt`, `ai/n.txt`, `ai/h.txt` and `ai/a.txt` (the `*_ai.txt` names were never
released and are not read), and a `.kmp` map's own `ai/` folder may carry them
too: they travel in the map package (`src/net/mappackage.cpp`'s AI-file names),
and the server applies a level's game-mod file, else the map package's
(`ai::levelProfile`, per room in `src/server/server.cpp` instead of per data set).
The DEFAULT AI's `ai/<aiprofile>.txt` / `ai/default.txt` lookup follows the same
rule for `ai/` files (game mod, else map package, else base; it was map over game
mod before); other map content keeps its layering. No shipped map or mission names
an `aiprofile` that collides with a level file. The Cartographer neither adds an
`ai/` folder to a `.kmp` nor keeps one when it saves a map again (it reads only
`kmap/` entries); an `ai/` folder added to a finished `.kmp` with an HPI packer
travels to the server with the map. Protocol stays 290.

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
  `CreateGame`'s private flag), then `c44968dd` (the setting defaults to
  CLASSIC).
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
  `mods/` packs and `ModCache/*.takmods` carry over. No settings key was added
  or removed (the prereleases' `campaignAi` key is ignored if present). Campaign
  missions now play DEFAULT AI (Classic) opponents. A game pack written for 0.8.10 that
  happens to carry an `ai/d.txt` ... `ai/ad.txt` would now adjust that AI
  level (so would a `.kmp` map carrying one); a game pack's `ai/default.txt` no longer changes our levels (it now
  changes Classic only).

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
  | Classic (Retail) | 300 s | `30483dcba73d108f` (16), twice | new level |

  The Retail, Legion and Classic 60 s games and the Retail and Legion 300 s
  games come from the landing run at `d86d5904`; the two Classic 300 s games
  were run for this report (`build-o2` rebuilt at `d86d5904`, `--version`
  `v0.8.10-36-gd86d5904fa47`; logs `~/tak-tmp/scratch/release-0811/mpai/`).
  Every pair reproduced. The Classic pair is the 0.8.11 reference.

- **Campaign Normal byte-identity** (from `e89d1f1a`): `takmission08_dh` 300 s
  `68a4b1588012db76` and `takx11_mt` 240 s `c5e3b08d1f08ba9e`, the same before
  and after the Opponent AI change.
- **Campaign Classic default** (`c44968dd`, `build-o2`, all targets rebuilt):
  `settings_test` checks the Classic default, the missing-key / unknown-value
  fallback, `normal` and the written forms; `ctest -L quick` 489/489 (186 s)
  and the campaign and settings tests (`campaign*`, `settings`,
  `mod_settings`, `crusades_campaign`, 12 tests) passed. A fresh-profile
  `takclient game --campaign takmission08_dh` (scratch HOME, local takserver)
  saved `campaignAi = classic` and its server logged `... profile=mission08
  CLASSIC (ai/mission08.txt)`; the same launch with `campaignAi = normal` logged
  no CLASSIC. Headless `--mpmission takmission08_dh`, 300 s, `takserver
  --local --no-auth --seed 1`: Normal (harness default, and again with
  `TAK_AI_LEVEL=2`) `68a4b1588012db76` both, unchanged; Classic
  (`TAK_AI_LEVEL=6`) `5c35c83f41cbae5f`, `err=none end=timelimit`. Logs:
  `~/tak-tmp/scratch/campaign-classic/`.
- **Level files `ai/<x>.txt`, also from a `.kmp` map; game mod over map**
  (branch `task-ai-dir`, `build-o2`, all targets rebuilt): `--mpai` Inner
  Circle, seed 1, 60 s: Retail `b353410003e98dd5`, Legion (`TAK_LEGION=1`)
  `0825aeb9e8139203`, DEFAULT (`TAK_AI_LEVEL=6`) `14849c00bc6e2a19` twice, all
  unchanged, every game `err=none end=timelimit`. `ai_profile`,
  `ai_profile_data` (a map package carries `ai/d.txt` ... `ai/ad.txt`; the map's
  `ai/n.txt` adjusts Normal; a game mod's wins over it; DEFAULT's
  `ai/default.txt` game mod > map > base), `ai_mod_profile` (three hosted all-AI
  games on AI Circle, Easy Aramon + Normal Taros: the game pack's
  comment-only `ai/n.txt` wins over the map's ban, Normal builds `tarcage` /
  `targarg`; with no game-mod file the `.kmp`'s `ai/n.txt` stops the Normal seat
  only, Easy still builds `araat` / `aralode`; the game pack's `ai/n.txt` does
  the same; root, cosmetic and base install `n.txt` ignored throughout),
  `classic_ai_map_profile` (adds a game with both a game pack's and the map's
  `ai/default.txt`: the game mod's is used), `server_ai_level`, `settings`,
  `match_codec`, `ai_behavior`, `ai_behavior_data`, `classic_ai`,
  `classic_ai_data`, `map_transfer`, `mod_transfer` and `mod_packs` passed;
  `ctest -L quick` 491/491; `check-detmath.sh` OK.
- **Game-mod files in `ai/`, level 6 shown as DEFAULT** (branch `task-ai-dir`,
  `build-o2`, all targets rebuilt): `--mpai` Inner Circle, seed 1, 60 s: Retail
  `b353410003e98dd5`, Legion (`TAK_LEGION=1`) `0825aeb9e8139203`, DEFAULT
  (`TAK_AI_LEVEL=6`) `14849c00bc6e2a19` twice, all unchanged, every game
  `err=none end=timelimit`. `ai_profile` and `ai_profile_data` (a pack's loose
  `ai/n` file and an archive's are read; a root one, a base install archive's
  and a cosmetic pack's are not; the mod codec carries them), `ai_mod_profile`
  (a hosted all-AI game, Easy Aramon + Normal Taros on AI Circle, game mods on:
  every misplaced file banning the Normal seat's `tarcage` / `targarg` is
  ignored; the game pack's `ai/` file stops the Normal seat only, Easy still
  builds `araat` / `aralode`; file names then `*_ai.txt`, since renamed), `classic_ai_map_profile` (log lines now read
  `DEFAULT AI profile`), `server_ai_level`, `settings`, `match_codec`,
  `ai_behavior`, `ai_behavior_data`, `classic_ai`, `classic_ai_data`,
  `mod_transfer` and `mod_packs` passed; `ctest -L quick` 491/491;
  `check-detmath.sh` OK.
- **AI profiles and Classic-only campaigns** (branch `task-ai-profiles`,
  `build-o2`, all targets rebuilt): `--mpai` Inner Circle, seed 1, Retail 60 s
  `b353410003e98dd5` and 300 s `a3d953803a064191`, Legion 60 s
  `0825aeb9e8139203` and 300 s `ce5c4ba7562e53b9`, all unchanged (the built-in
  tables reproduce the profile the levels read before); Classic
  (`TAK_AI_LEVEL=6`) 60 s `14849c00bc6e2a19`, twice, unchanged. `ai_profile`,
  `ai_profile_data` (the built-in Normal table equals the install's
  `ai/default.txt`), `ai_mod_profile` (a real hosted game: a game pack's
  Normal file reaches only the server's Normal AI; since `task-ai-dir` it is
  `ai/n.txt`), `classic_ai_map_profile`
  (base, game-pack and map-package profiles each win in turn;
  `takmission08_dh` reads `ai/mission08.txt`), every `classic_ai*` test,
  `ai_behavior`, `ai_behavior_data`, `naval_ai`, `retail_ai`, `settings`,
  `mod_packs`, `mod_transfer`, `map_transfer`, `match_codec` and the campaign
  tests passed; `ctest -L quick` 491/491; `check-detmath.sh` OK.
- **Detmath guard** (`tools/check-detmath.sh`) at `d86d5904`: OK, no direct
  libm transcendentals in the sim path and no `std::atomic_ref`.
- **Cross-compiler golden hash** (`tools/check-determinism.sh`) at `d86d5904`:
  g++ and clang++ at -O0/-O2/-O3 all give `dcef618cd2e4d558`, the same golden
  as 0.8.10. The aarch64 legs were skipped (no target libc/headers locally);
  ARM64 parity is covered by the macOS CI leg. Log
  `~/tak-tmp/scratch/release-0811/check-determinism.log`.
- **Final source** (the preparation commits on `d86d5904`; only this report
  differed from the built tree): every `build-o2` target rebuilt (681 steps,
  clean), `takclient --version` reports `0.8.11`, and `ctest -L quick` passed
  489/489 (141 s; log `~/tak-tmp/scratch/release-0811/land-ctest-quick.out`).

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
committed on the screenshots branch (`task-release-0811-shots`, `f16e61c7`,
landed on main as `df0c5eef`) and
each inspected by eye: `docs/img/title.jpg` (VERSION 0.8.11, same 1440 × 1080
crop), `docs/img/campaign.jpg` (fresh profile, chapter 1, with the **Opponent AI**
line under the always-on mods note; retaken after the Classic default with the
same framing, so it reads **Opponent AI: CLASSIC (A)**; only that line
differs from the NORMAL picture, which a Normal capture of the new build
matched exactly) and the new
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
When the Opponent AI line was removed (branch `task-ai-profiles`),
`campaign.jpg` was retaken with the same framing (fresh profile,
`TAK_SHOT_CAMPAIGN=0`, 1600 × 1000, quality 88) and no longer shows the line;
the book's alt text and caption were updated. When level 6 became DEFAULT in
the lobby (branch `task-ai-dir`), the lobby, room, diplomacy and scorecard
pictures were checked again: none shows level 6, so none was retaken.

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
- The DEFAULT AI's (Classic's) accepted limits (from the
  [acceptance ledger](classic-ai-acceptance.md#accepted-limits)): the
  simulation is ours, decisions land a tick later and the brain has its own RNG
  stream, so games differ from retail's even where every rule matches; the
  target picker's unit cap is the lobby's (default 2000), not retail's 150 /
  500; no Classic game test near the unit cap (U8); exact ties in the Monarch's
  retreat read statically; U6, U14 and V8 / V11 leftovers; campaign start
  records inferred; order glue (U1, U13); canbuild menu order, seeding and the
  visible-enemy list follow our engine. The DEFAULT AI is weaker than Normal
  (loses in 8 to 18 game-minutes in tests) and a Taros DEFAULT AI often grows
  slowly.
- Testing was reduced (above): one full `-O2` suite, the `--mpai` set, no
  other trees and no remote sweep.
- TODO-LEAD: flakes seen in CI, if any.
