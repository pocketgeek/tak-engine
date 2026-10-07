# 0.7.27 release validation

Protocol **238**, replay format **11**, generator version **8**, campaign
payload **4**, database schema **9**.

Status: **in preparation.** Version 0.7.27 has not been tagged or published.
Its scope is everything since [0.7.26](release-0.7.26-validation.md); see the
[release notes](release-0.7.27-notes.md).

## Scope and compatibility

Four optimizations for very large battles. Three Legion bookkeeping changes
(live-field totals, a member index per formation point, recounting only changed
soft-obstacle cells) and Retail's per-unit path-pending flag are
behavior-identical. Two further Legion changes alter Legion games: held units
rest between updates, and far-apart static-map changes are repaired one cluster
at a time. The Legion navigation golden was regenerated for them; the Retail
golden is unchanged. Tactical Dots and the Renderer option are client-only
display changes with no simulation, network or hash effect. The Linux and
Windows CI workflows were changed so packages report their build identity
(confirmed in CI; see below). Options greys out Tactical Dots Zoom while Tactical
Dots is off.

Protocol 238 is required for live play. Replays need the exact simulation
protocol, so protocol-237 (0.7.26) recordings are not playable by this build.
Replay format 11, generator version 8, campaign payload 4 and database schema 9
are unchanged since 0.7.26 (checked in source against `v0.7.26`).

## Local preparation checks

Pending.

## Remote multiplayer sweep

Pending.

## CI and packages

Pending for the tag. The build-identity fix is confirmed on the version commit
`9642e70`: all 16 Linux package jobs
([Linux run](https://github.com/pocketgeek/tak-engine/actions/runs/37688998787))
and both Windows builds
([Windows run](https://github.com/pocketgeek/tak-engine/actions/runs/37688998842))
report the clean id `9642e70e009b`, with no `build unknown` and no `-dirty`. On
the tag the id will read `v0.7.27`.

## Screenshots

Done on 2026-10-07 with the 0.7.27 release-preparation build (`takclient
--version` 0.7.27; Debug with `-O2`, OpenGL; servers report protocol v238) and a
capture-only patch that is not committed. Each new or re-rendered image was
inspected by eye against its caption.

- **Gallery.** Re-rendered: `title.jpg` (shows VERSION 0.7.27) and `hud.jpg`
  (the stats panel now ends with the new RENDERER row, showing GL). The other
  gallery images are unchanged from 0.7.26; none shows the stats panel, the
  Options screen or Tactical Dots.
- **User guide.** New: `guide/tactical-dots.jpg`, the same moment of a
  deterministic Benchmark battle rendered with Tactical Dots off and on, side
  by side, inserted in the Tactical Dots text with alt text and a caption.
  Re-rendered: `guide/options-graphics.jpg`, showing Renderer (AUTO) first in
  Graphics, Tactical Dots (OFF) and Tactical Dots Zoom (20%, greyed out
  because Tactical Dots is off), all at their defaults in a fresh profile. `guide/hud-annotated.jpg` was recaptured for
  checking but kept: at 1600 × 900 the stats panel shows only its first four
  rows, so the RENDERER row is not visible there.
- **Labels.** The development and Benchmark scenes are identified in the
  captions and in [the capture notes](img/README.md).
- **Size.** Total tracked image bytes rose from 14.71 MB to 15.49 MB (+0.78 MB, almost all the new Tactical Dots picture).
- **Links.** All relative links, anchors and image paths in the changed
  Markdown resolve.
- **Not captured.** The first-launch folder picker is the operating system's
  own dialog. The Renderer fallback notice and the non-OpenGL backends were not
  photographed.

## Limits

Pending.
