# 0.7.28 release validation

Protocol **253**, replay format **12**, generator version **8**, campaign
payload **4**, database schema **9**.

**Pending:** version 0.7.28 has not been tagged or published yet. This report
is completed as the checks below are run.

## Scope and compatibility

0.7.28 follows 0.7.27 (protocol 238) and carries protocols 239 to 253:

- **Legion plan W0–W9 (protocols 239–248).** Shared goal fields, one convoy
  per click, the settle rule, upkeep on demand, reach rings, flyer stations for
  squads, paused first builds, crossing-group corridors, the passage gate and
  head-on meets. Workstreams that changed Legion behaviour regenerated the
  Legion navigation golden and re-licensed the scenario baseline.
  Eleven harder targets were accepted as known limits at the close of the plan
  (user decision W9-1; see `docs/legion-pathfinding.md`).
- **Engine-wide performance (WE).** The simulation steps are exact: as their
  commits record, each was checked with `tools/legion_identity.sh` against its
  base (replays, goldens, `--mpai` games, crowdbench rows and the determinism
  golden SAME).
  Protocol 249 adds client progress acks and ping-scaled flow control (no sim
  or hash change); the presentation clock and jitter buffer are client-only.
- **Game speed (protocol 250).** The original's levels −9..+10 (0.1×–2×),
  pacing and setup only, no hashed change in a normal game; release servers
  clamp to 2×. Protocol 250 also carries the staged stress fill.
- **Blocked building sites (protocol 251)** and the **fire-at-will sweep with
  deterministic per-player vision, AI weapon switching and idle retaliation
  (protocol 252)**, plus the sweep's `shootme` test (protocol 253). These change
  hashed simulation state in both pathfinding modes; the Retail and Legion
  navigation goldens and the Legion battle baseline were regenerated for 252.
- **AI-side changes** (Defensive AI against unseen attackers, the AI Monarch
  fighting back, Defensive build sites out of fights) issue recorded commands
  only and need no protocol change.
- **Client-only changes:** build-menu portraits with Smooth GUI Art, Max FPS
  greyed out under VSync, VSync presentation pacing, the Windows
  display-scaling option, and the stats-panel and Benchmark speed level.
- **Replay format 12** adds a position digest to checkpoints; format-11 files
  still load. Directory-wide `-ffp-contract=off` fixes a macOS-hosted game
  diverging from Linux and Windows without changing Linux or Windows code.

Protocol 253 is required for live play. Replays need the exact simulation
protocol, so protocol-238 (0.7.27) recordings are not playable by this build.
Generator version 8, campaign payload 4 and database schema 9 are unchanged
since 0.7.27 (checked in source against `v0.7.27`: `git diff` touches none of
`src/tnt/mapgen*`, `src/net/crusades.h` or the Crusades store; the replay
header change is the format-12 digest).

## Local preparation checks

Pending.

## Remote multiplayer sweep

Pending.

## CI and packages

Pending.

## Screenshots

Done on 2026-10-11 with the 0.7.28 release-preparation build (`takclient
--version` 0.7.28; Debug with `-O2`, OpenGL) and a capture-only patch that is
not committed. Each new or re-rendered image was inspected by eye against its
caption.

- **Gallery.** Re-rendered: `title.jpg` (shows VERSION 0.7.28) and `hud.jpg`
  (the stats panel's SIM row now reads NORMAL, a game-speed level). The other
  gallery images are unchanged; none shows the stats panel, the Options screen,
  or a build-menu portrait with Smooth GUI Art on.
- **User guide.** New: `guide/fire-on-the-move.jpg`, a two-player development
  scene of Archers and Mage Builders shooting on a plain move order, inserted in
  [Your army](user-guide.md#your-army) with alt text and a caption.
  Re-rendered: `guide/hud-annotated.jpg` (SIM reads NORMAL; the selected-Monarch
  marker moved to where Elsin stands) and `guide/options-graphics.jpg`, now
  showing the Display section with Max FPS greyed out under VSync above the
  Graphics settings, moved to the guide's [Display](user-guide.md#display)
  section. The Windows-only display-scaling row is absent from this Linux
  capture.
- **Labels.** The development scene is identified in its caption and in
  [the capture notes](img/README.md).
- **Size.** Total tracked image bytes rose from 15.48 MB to 15.78 MB (+0.30 MB,
  mostly the new picture).
- **Links.** All relative links, anchors and image paths in the changed
  Markdown resolve.
- **Not captured.** Smooth GUI Art build-menu portraits (off by default in
  every capture profile), the Windows display-scaling row, and the in-game
  speed banner.

## Limits

Pending.
