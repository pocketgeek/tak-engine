# 0.8.13 release validation

Protocol **293** (unchanged from 0.8.12), replay format **14**, generator
version **9**, campaign payload **4**, database schema **9**.

TODO-LEAD: tag (`v0.8.13` at `<sha>`, annotated tag object `<obj>`) and
publication date/time.

## Scope and compatibility

0.8.13 follows 0.8.12 (protocol 293). After the 0.8.12 validation commit
`1745b3d2`, main reached `c579cab5` in 3 commits, all linear (no merges).
`git diff --stat v0.8.12..c579cab5 -- src` touches 9 files: `src/cartographer/`
(`aiprofileswindow.cpp`, `chrome.cpp`, `dialogs.cpp`, `editorstate.h`,
`main.cpp`, `session.cpp`) and `src/client/` (`gameview.h`,
`gameview_hud.cpp`, `gameview_render.cpp`). Nothing under `src/sim/`,
`src/net/`, `src/ai/`, `src/server/`, `src/campaign/`, `src/tnt/` or
`src/net/replayhdr.h` changed. The other changes are tests
(`tools/cartographer_ui_test.cpp`, `tools/cartographer_placement_test.cpp`, two
new `add_test` lines in `CMakeLists.txt`) and docs (`docs/cartographer-guide.md`,
`docs/user-guide.md`).
TODO-LEAD: list the preparation commits on top of `c579cab5` (docs, any shots,
this report's fill) and confirm `git fetch` showed nothing else on
`origin/main` before landing.

The changes:

- In game: `a41beb28` (the armed-order text banner, such as **ATTACK: CLICK
  TARGET**, and the objectives-panel offset it needed are removed; retail shows
  an armed order only by its cursor and pressed order button).
- Cartographer: `90342ab7` (a bare launch opens an empty window, title
  **Cartographer**, **NO MAP OPEN**; `Session::noDocument` and
  `Editor::noMap()`; map commands, tools and canvas input inert; the empty
  window is never dirty; cancelling New/Open or closing an AI mod returns to
  it), `c579cab5` (held Esc no longer quits; the failed TEST IN GAME message is
  worded by what was tested, `playtestAiMod`; a map that fails to open at launch
  ends on the empty window after OPEN FAILED).

No hashed simulation state changed: no commit touches `src/sim/`, `src/net/`,
`src/ai/` or `src/server/`.

**Compatibility.** `kNetVersion` is **293** in `src/net/protocol.h` at
`c579cab5`, as at `v0.8.12`. `kReplayFormat` is **14** in
`src/net/replayhdr.h`, as at `v0.8.12`. The map generator's `formatVer` **9**
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` 4
(`src/net/crusades.h`) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`) are unchanged.

- **Multiplayer.** `Server::handshake` (`src/server/server.cpp`) rejects a
  Hello whose protocol differs from `kNetVersion` or whose pure-retail
  gameplay-data hash differs from the referee's. The Hello's build id is read
  and ignored (`(void)build`), and the release string `tak::kVersion` is never
  sent in the handshake. So **0.8.13 and 0.8.12 clients and servers play
  together in any mix**; 0.8.11 or earlier (protocol 290 or lower) cannot join
  either, nor either join an older server.
- **Replays.** `supportedReplayProtocol` accepts a recording when its format is
  1..14 and its protocol equals `kNetVersion`; the header's `engineVersion` is
  only displayed. So 0.8.13 plays 0.8.12's protocol-293 replays and 0.8.12
  plays 0.8.13's; 0.8.11's protocol-290 replays are refused.
- **Maps, mods and settings.** Saved `.kmp` maps, generator-8/9 recipes,
  `mods/` packs and `ModCache/*.takmods` carry over. No settings key was added
  or removed.

TODO-LEAD: optionally record a mixed-version check (a 0.8.12 release client
joining a 0.8.13 server, or the reverse, and a 0.8.12 replay played on 0.8.13),
or state that none was run and compatibility rests on the unchanged protocol
and source above.

## Local preparation checks

Testing is reduced for this release, by the user's rule (minimal testing):

- **Full CTest, once, on optimized Debug (`-O2`) at `c579cab5`:**
  TODO-LEAD (filled by the lander): passed/total, `-LE nightly -j16`, real
  time, `rc`, defined vs excluded counts, timeouts/reruns, scratch HOME and
  XDG_DATA_HOME. Name the new and changed tests inside the run:
  `cartographer_blank_launch_ui`, `cartographer_failed_open_ui`,
  `cartographer_aimod_ui`, `cartographer_starts_ui`,
  `cartographer_place_blocked`. Log:
  `~/tak-tmp/scratch/release-0813/ctest.out`.
- No Release, `-O0` Debug, Clang or ASan suite was run, and no repeat runs.
- **Detmath guard** (`tools/check-detmath.sh`): TODO-LEAD.
- **Cross-compiler golden hash** (`tools/check-determinism.sh`): TODO-LEAD
  (0.8.12 golden `dcef618cd2e4d558`).
- **`--mpai` golden hashes.** Inner Circle, `takserver --local --no-auth
  --seed 1`, a clean scratch profile per game, optimized Debug binaries at
  `c579cab5`, `TAK_LEGION=1` for Legion and `TAK_AI_LEVEL=6` for the Retail AI.
  TODO-LEAD: confirm every game ended `err=none end=timelimit` with no desync
  or referee-suspect line, and fill the units column.

  | Mode | Length | Hash (units) | 0.8.12 |
  | --- | --- | --- | --- |
  | Retail | 60 s | TODO-LEAD | `b353410003e98dd5` |
  | Retail | 300 s | TODO-LEAD | `a3d953803a064191` |
  | Legion | 60 s | TODO-LEAD | `0825aeb9e8139203` |
  | Legion | 300 s | TODO-LEAD | `ce5c4ba7562e53b9` |
  | Retail AI | 60 s | TODO-LEAD | `14849c00bc6e2a19` |

  Logs: `~/tak-tmp/scratch/release-0813/mpai/`.
- **Final source** (the preparation commits on `c579cab5`): TODO-LEAD (every
  `build-o2` target rebuilt, `takclient --version` reports `0.8.13 (build
  ...)`, any quick check run).

## Remote multiplayer sweep

**Not run for this release.**
TODO-LEAD: confirm the justification. No commit since `v0.8.12` touches
`src/sim/`, `src/net/`, `src/ai/` or `src/server/` (only the client HUD and the
Cartographer changed), the protocol stays 293, and the `--mpai` gates above
match 0.8.12. The sweep's 37 scenarios would replay 0.8.12's table (in turn
0.8.10's, `docs/release-0.8.10-validation.md`).

## CI and packages

TODO-LEAD: main CI on the final source (Windows, macOS, Linux packages,
Determinism run ids and attempts).

TODO-LEAD: tag CI on the exact tag (Determinism dispatched on the tag, Linux,
Windows, macOS incl. Sign + notarize), asset count (29 expected) and digests,
size deltas vs 0.8.12, dependencies, file lists, build ids (`v0.8.13`),
Windows Authenticode (9 signed files expected), macOS notarization, packaged
server Retail AI run, packaged client/editor command lines (a packaged
`cartographer` started with no map should now open the empty window, if
checked), streaming muxer, static Opus, font licences, no `mods/`/`overrides/`
or `click.hpi`, macOS microphone and Retina keys, `blob/v0.8.13/docs/...` links
in the release body.

TODO-LEAD: `--mpai` on Windows and macOS (optimized Debug of the tag on
oden-win and the arm64 MacBook; five games each, expected to equal Linux).

TODO-LEAD: publication (`gh release edit v0.8.13 --draft=false --latest`, date
and time, asset count).

## Screenshots

Retaken on branch `task-release-0813-shots` (`9e42f24c`):

- `docs/img/title.jpg`: VERSION 0.8.13, from an `-O2` build with the version
  set to 0.8.13 locally (`~/tak-tmp/scratch/release-0813/shots/title.py`); a
  pixel diff against the 0.8.12 file shows only the version digits changed.
  The README title alt text and gallery footnote ("0.7.25 to 0.8.13 builds")
  follow it.
- `docs/img/cartographer/new-map.jpg`: retaken; the New Map dialog opened with
  **File → New map** over the dimmed empty window (NO MAP OPEN in the status
  bar), whole 921 × 691 window.
- `docs/img/cartographer/empty-window.jpg`: new; the empty window of a bare
  launch, added to the guide's "The first screen".

Both Cartographer pictures come from the `cartographer_ui_test` workflow
`blank-launch` (passed) with `TAK_EDITOR_TEST_CAPTURE`, SDL `offscreen` and a
scratch profile, plus a capture-only patch, not committed
(`~/tak-tmp/scratch/release-0813/shots/carto-capture.patch`).
`docs/img/README.md` records both. No picture showed the removed armed-order
banner (the top-left area of every gallery, guide and quick-start picture was
checked), so none was retaken for it; every other picture is kept.

## Limits

- TODO-LEAD: state whether a game mixing 0.8.13 with 0.8.12 was attempted.
- All of 0.8.12's limits carry over: Legion units stand and wait more than
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
  accepted limits ([acceptance ledger](classic-ai-acceptance.md#accepted-limits));
  our six levels' built-in tables are identical, so `ai/takengine.txt` sets the
  same weights and limits for every level; Cartographer **TEST IN GAME**
  (`--game-mod`) has not been tried by hand on Windows or macOS.
- Accepted from 0.8.12 and unchanged: the Retail AI server log still says
  `DEFAULT AI profile ai/DEFAULT.txt`, and the Cartographer's tab still reads
  **DEFAULT AI**.
- TODO-LEAD: tag CI flakes, reruns or force-cancels, if any.
