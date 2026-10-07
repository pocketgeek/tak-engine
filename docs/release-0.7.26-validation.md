# 0.7.26 release validation

Protocol **237**, replay format **11**, generator version **8**, campaign
payload **4**, database schema **9**.

Status: **in preparation.** Version 0.7.26 has not been tagged or published.
Its scope is everything since [0.7.25](release-0.7.25-validation.md); see the
[release notes](release-0.7.26-notes.md).

## Scope and compatibility

Grouped flyer movement changes in both pathfinding modes. Retail ports the
original game's VTOL_Move group checks and VTOL_Move_Formation, including the
1-in-4 order drop for a flyer found ahead of its group a second time. Legion
flyers in a formation with ground members hold stations over the ground
members and keep to their pace. AI games issue no groups, so Retail AI games
are expected to be unaffected.

Protocol 237 is required for live play. Replays need the exact simulation
protocol, so protocol-236 (0.7.25) recordings are not playable by this build.
Replay format 11, generator version 8, campaign payload 4 and database schema 9
are unchanged since 0.7.25 (checked in source against `v0.7.25`).

## Local preparation checks

Pending.

## Remote multiplayer sweep

Pending.

## CI and packages

Pending.

## Screenshots

Done on 2026-10-07 with the 0.7.26 release-preparation build (`takclient
--version` 0.7.26; Debug with `-O2`; servers report protocol v237). Every image
was rendered fresh, except the nine UI views carried over from 0.7.25 (listed
in the notes), and each was inspected by eye against its caption.

- **Gallery.** New: `battle.jpg`, `dragons.jpg`, `flyers.jpg`, `hud.jpg` and
  `generated.jpg`. Re-rendered: `title.jpg` (shows VERSION 0.7.26) and
  `naval.jpg`. Removed: `army.jpg` and `gameplay.jpg`.
- **User guide.** Thirteen new images in `docs/img/guide/`, plus seven reused
  gallery images. All 20 are referenced from `user-guide.md` with alt text and
  a caption.
- **Labels.** Development scenes, debug fixtures and sample data are identified
  in the captions and in [the capture notes](img/README.md).
- **Size.** Total tracked image bytes rose from 7.06 MB to 14.7 MB.
- **Links.** All relative links, anchors and image paths in the changed
  Markdown resolve.
- **Not captured.** The first-launch folder picker is the operating system's
  own dialog.

## Limits

Pending.
