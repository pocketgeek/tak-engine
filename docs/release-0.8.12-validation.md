# 0.8.12 release validation

Protocol **293** (was 290), replay format **14**, generator version **9**,
campaign payload **4**, database schema **9**.

TODO-LEAD: Tagged `v0.8.12` at `<sha>` (annotated tag object `<obj>`) and published on `<date> UTC`.

## Scope and compatibility

0.8.12 follows 0.8.11 (protocol 290). After the 0.8.11 validation commit
`828a24ee`, main reached `450a10b6` in 37 commits, all linear (no merges).
`git diff --stat 828a24ee..450a10b6 -- src` touches 38 files: `src/ai/`
(`builtinprofile.cpp`, `builtinprofile.h`); `src/cartographer/`
(`aiprofiles.cpp` and `aiprofiles.h` (new), `aiprofileswindow.cpp` (new),
`canvas.cpp`, `chrome.cpp`, `dialogs.cpp`, `document.cpp`, `document.h`,
`editor.h`, `editorstate.h`, `history.cpp`, `history.h`, `main.cpp`,
`palette.cpp`, `playtest.cpp`, `playtest.h`, `savejob.cpp`, `savejob.h`,
`session.cpp`, `validation.cpp`, `validation.h`); `src/client/`
(`datafolder.h`, `gameview.h`, `gameview_hud.cpp`, `gameview_lobby.cpp`,
`gameview_render.cpp`, `main.cpp`, `settings.cpp`, `settings.h`); `src/hpi/`
(`hpi.cpp`, `hpi.h`); `src/net/` (`mappackage.cpp`, `protocol.h`);
`src/server/server.cpp`; and `src/sim/` (`sim.cpp`, `sim.h`). Nothing under
`src/campaign/`, `src/tnt/`, `src/terrain/`, `src/cob/`, `src/crt/`,
`src/video/` or `src/net/replayhdr.h` changed.
TODO-LEAD: list the preparation commits on top of `450a10b6` (screenshots,
version 0.8.12 / notes / docs, this report's fill) and confirm nothing else
landed on main (checked with `git fetch` before landing).

The changes:

- Cartographer keeps a map's `ai/` files: `baa0d0d1` (open, save, Test Map,
  recovery, undo), `95083284` (an installed map opened by name), `0b901740`
  (docs).
- Map packages (protocol 291): `d9e32a1b` (a map's AI files come from its own
  archive, `Vfs::readFromMapArchive`), `62f68894`, `610e0096` (docs).
- Cartographer AI Profiles window: `654657ef` (model checked against the
  engine's parsers), `fdf825e7` (window), `1e8fcb50` (UI tests), `5862d171`
  (game-mod writer: plan, backups, atomic install), `9de87966` (Save as / Open
  from game mod), `9f51b0e6` (tests), `e37deb2f`, `065bd4c3` (group rules, pack
  roster, Ctrl+Z, limits in play), `129358fb` (extras test), `f24224d2` (end to
  end), `12eb7a86` (guides), `5f8273d6` (case-only rename), `da0f2de1` (a
  Retail profile saved as `ai/default.txt` names that file), `a2374397` (pack
  list scrolls), `80083f65` (move a group rule down), `96471330` (copy to other
  levels; replaced by `ca9b612d`), `3ed1b8b3`, `fcc6d27b`; ledger `95673f1c`,
  `6eee1837`, `8776dbad`, `c1aa2990`, `d9840695`.
- Campaign: `4de33a79` (order hint below the objectives panel), `c51dc03a`
  (missions always allow speed changes; `src/server/server.cpp` sets
  `speedUnlock` for a mission room).
- Lobby: `2c6b89a9` (AI level 6 shown as RETAIL; `aitool retail`).
- Flyers (protocol 292): `1e9d7df3` (a Shift-queued patrol of an armed or
  builder flyer laps; `World::openFlightPatrol`, `movement_orders`
  `queuedFlyerPatrols`).
- AI files (protocol 293): `40958301` (our six levels read one
  `ai/takengine.txt` over their own tables), `ca9b612d` (one TAK ENGINE AI tab,
  MERGE INTO TAK ENGINE), `7a040a0f` (AI mod without a map, TEST IN GAME,
  release-safe `--game-mod`), `450a10b6` (footer wording, `ai-mod.jpg`).

The only hashed simulation change is `1e9d7df3` (queued flyer patrols; its own
test failed 6 checks on the old code). Protocols 291 and 293 change map and
game-mod packages, not the simulation; `c51dc03a` only sets a campaign
mission room's speed-change option on the server. With no `ai/takengine.txt` in play our
levels play their built-in tables, as in 0.8.11. `--mpai` Inner Circle seed 1
is unchanged from 0.8.11 at 60 s and 300 s in both modes, and for a Retail AI
seat at 60 s (see the local checks).

TODO-LEAD: `--mpai` on Windows and macOS (optimized Debug builds of the tag,
oden-win and the arm64 MacBook): Retail 60 s `b353410003e98dd5` / 300 s
`a3d953803a064191`, Legion 60 s `0825aeb9e8139203` / 300 s
`ce5c4ba7562e53b9`, Retail AI (`TAK_AI_LEVEL=6`) 60 s `14849c00bc6e2a19`;
`err=none end=timelimit`; no desync or referee-suspect line.

**Compatibility.** `kNetVersion` is **293** in `src/net/protocol.h` at
`450a10b6` (290 at `v0.8.11`). `kReplayFormat` is **14** in
`src/net/replayhdr.h`, as at `v0.8.11`. The map generator's `formatVer` **9**
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` 4
(`src/net/crusades.h`) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`) are unchanged.

- **Multiplayer.** The server's Hello check rejects a client whose protocol
  differs from `kNetVersion` (or whose gameplay-data hash differs). So **0.8.12
  cannot play with 0.8.11 or earlier in either direction**; the server and
  every player must update together.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  protocol equals `kNetVersion`, so 0.8.12 refuses 0.8.11's protocol-290
  replays (format 14 as well) and 0.8.11 refuses 0.8.12's.
  `tools/replay_test.cpp` pins `kNetVersion == 293` and `kReplayFormat == 14`
  and checks that `supportedReplayProtocol(14, 292)` is refused.
- **Maps, mods and settings.** Saved `.kmp` maps, generator-8/9 recipes,
  `mods/` packs and `ModCache/*.takmods` carry over. No settings key was added
  or removed (`--game-mod`'s pack selection is for one run and never saved over
  the player's ticks). A game pack or `.kmp` map made for 0.8.11 that carries
  `ai/d.txt` ... `ai/ad.txt` no longer adjusts our levels (the files are not
  read; a map package naming one is refused); `ai/takengine.txt` replaces them.

## Local preparation checks

Testing is reduced for this release, by the user's rule (minimal testing):

- TODO-LEAD: **Full CTest, once, on optimized Debug (`-O2`) at `450a10b6`:
  `<n>/<n>` passed** (`<time>` real). Log: `<path>`.
- No Release, `-O0` Debug, Clang or ASan suite was run, and no repeat runs.
- TODO-LEAD: **`--mpai` golden hashes.** Inner Circle, `takserver --local
  --no-auth --seed 1`, scratch profile, optimized Debug binaries at
  `450a10b6`, `TAK_LEGION=1` for Legion and `TAK_AI_LEVEL=6` for the Retail AI.

  | Mode | Length | Hash (units) | 0.8.11 |
  | --- | --- | --- | --- |
  | Retail | 60 s | TODO-LEAD | `b353410003e98dd5` |
  | Retail | 300 s | TODO-LEAD | `a3d953803a064191` |
  | Legion | 60 s | TODO-LEAD | `0825aeb9e8139203` |
  | Legion | 300 s | TODO-LEAD | `ce5c4ba7562e53b9` |
  | Retail AI | 60 s | TODO-LEAD | `14849c00bc6e2a19` |

  Logs: `<path>`.
- Landing checks recorded in the commits: `40958301` and `1e9d7df3` each
  report `--mpai` Inner Circle seed 1 60 s unchanged (Retail
  `b353410003e98dd5`, Legion `0825aeb9e8139203`, Retail AI
  `14849c00bc6e2a19`); `ca9b612d` reports `ctest -L quick` 497/497 and
  `cartographer_ai_profile_e2e` passing on `build-o2`.
- TODO-LEAD: **Final source** (the preparation commits on `450a10b6`): every
  `build-o2` target rebuilt, `takclient --version` reports `0.8.12`, and
  `ctest -L quick` `<n>/<n>`.

## Remote multiplayer sweep

**Not run for this release.**
TODO-LEAD: confirm this justification. The `--mpai` gates are unchanged from
0.8.11 (Retail and Legion at 60 s and 300 s, the Retail AI at 60 s), and the
only simulation change is the Shift-queued armed/builder flyer patrol fix
(`1e9d7df3`, protocol 292), which has its own test (`movement_orders`
`queuedFlyerPatrols`, failing 6 checks on the old code, passing in Retail and
Legion). Protocols 291 and 293 change only which AI files map and game-mod
packages carry. The sweep's 37 scenarios would otherwise replay 0.8.11's
(0.8.10's) table (`docs/release-0.8.10-validation.md`).

## CI and packages

TODO-LEAD: main CI on the final source (Windows, macOS, Linux packages,
Determinism); tag CI runs (Determinism dispatched on the tag, Linux, Windows,
macOS with Sign + notarize); the 29 assets downloaded and checked against
GitHub's SHA-256 digests and sizes; build ids `0.8.12 (build v0.8.12)`;
Windows Authenticode (9 signed files); macOS notarization (DMGs, apps, bare
programs); packaged server runs the Retail AI (`TAK_AI_LEVEL=6 --mpai --time
60`, Inner Circle, `14849c00bc6e2a19`); Opus static, FFmpeg MP4 muxer, font
licences, no `mods/` or `overrides/` in packages, macOS microphone and Retina
keys; `docs/release-0.8.12-notes.md` and this report at the tag; every
`blob/v0.8.12/docs/...` link in the release body resolves.

TODO-LEAD: Published with `gh release edit v0.8.12 --draft=false --latest` on
`<date> UTC`.

## Screenshots

TODO-LEAD: what was retaken by the 0.8.12 build (at least `docs/img/title.jpg`
for VERSION 0.8.12; the README title alt text and gallery footnote and
`docs/img/README.md` follow), and what was checked and kept. The Cartographer
AI window captures (`docs/img/cartographer/ai-profiles.jpg`,
`ai-group-rules.jpg`, `ai-takengine.jpg`, `ai-mod.jpg`) were made by the
`cartographer_aiprofile*_ui` tests' capture mode as the features landed (see
`docs/cartographer-improvements-progress.md`).

## Limits

- No game mixing 0.8.12 with 0.8.11 or older was attempted: the protocols
  differ, so the server refuses it.
- All of 0.8.11's limits carry over: Legion units stand and wait more than
  Retail's; Legion attackers through a narrow gate deal more damage than
  Retail's; power codes are lockstep (retail applied them only on the typer's
  machine); FULL VISION does not switch LOS off at start; UI FONT CLASSIC is not
  pixel-identical with 0.8.7; the Legion bridge lottery; the Legion 10,000
  workloads' congestion and tick budget, mixed-10000's hovering flyers; Retail
  `h-cramped` ends early; the macOS Retina UI Scale is migrated once; a
  stationary auto-targeting shooter keeps refusing a once-blocked target; the
  campaign book's chapter numeral touches "APTER"; a server-side upload stall
  longer than 1 s is charged to every seat it holds up; a reconnecting seat
  that hitches while catching up is charged as a hold; the Retail AI's
  accepted limits ([acceptance ledger](classic-ai-acceptance.md#accepted-limits)).
- New: our six levels' built-in tables are identical
  (`src/ai/builtinprofile.cpp`: every level's change list is empty), so
  `ai/takengine.txt` sets the same unit weights and limits for every level;
  they still differ in `ai::paramsFor` (Easy `limitScale` 50, Hard and Absurd
  150, the Defensive levels unbounded except a limit of 0) and behaviour.
- New: Cartographer **TEST IN GAME** (`--game-mod`) has not been tried by hand
  on Windows or macOS.
- TODO-LEAD: flakes seen during tag CI, if any.
