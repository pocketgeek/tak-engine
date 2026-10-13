# 0.8.0 release validation

Protocol **256**, replay format **12**, generator version **8**, campaign
payload **4**, database schema **9**.

**Pending:** version 0.8.0 has not been tagged or published yet. This report
is completed as the checks below are run.

## Scope and compatibility

0.8.0 follows 0.7.28 (protocol 253) and carries protocols 254, 255 and 256:

- **Legion formation flyers over the front (protocol 254).** Flyer stations,
  positions and the flyer order point in squads that mix ground units and
  flyers (Legion only; Retail exits first). As the commits record, the seeded
  `--mpai` Inner Circle 300 s hashes were unchanged in both modes (Retail
  `95b05c8290b55317`, Legion `21055e66adec40d6`), since that AI game forms no
  mixed squads; `legion_world_test` (mixedformation, flyerfront) and
  `legion_flyers_test` cover the change.
- **Factory output site admission (protocol 255).** A scripted producer's
  ground output is admitted by the footprint-strict mobile placement test (icd
  401deb stage 2, 507d10), and a refused site waits `rand(15)+7` ticks on the
  hashed `Unit::productionRetryTick`. Retail's GetBuilt release (rally or
  PARK) is documented but not ported. `production_exit` adds 228 cases (every
  shipped ground factory); the `--mpai` hashes were unchanged (no site is
  refused there), and `check-determinism` gave `dcef618cd2e4d558` on the
  landing commit. See `docs/factory-output-2026-10-12.md`.
- **Air transport unload standby (protocol 256).** An air carrier whose unload
  empties its order list takes its default mission in the same call that
  retires VTOL_UNLOAD (icd 4d8450/4d856a), with the step-out controller still
  attached, so VTOL_Standby lands it two ticks on instead of flying it
  1,048-1,496 px along that controller (zonroc, tarship, creaeri);
  `World::standbyEligible` no longer waits for an active retained controller.
  `tools/re/probe_transport_air_unload_default_mission.py` runs the real
  dispatcher and unload. `transport_post_unload` covers every shipped carrier
  (13 standard, 14 Crusades), Retail and Legion, one passenger and a full load
  (204 cases, all 48 air cases failing before), plus 24 queued-order cases. As
  the commit records, ctest on build-o2 passed 552/552, the seeded `--mpai`
  Inner Circle 300 s hashes were unchanged (Retail `95b05c8290b55317`, Legion
  `21055e66adec40d6`; no unload happens there) and `check-determinism` gave
  `dcef618cd2e4d558`. Documented deviation: an ordinary flyer move's detached
  coast still settles before standby.
- **Display-only simulation-adjacent changes.** The Legion walk-animation pace
  runs in the client's display COB VM; the authoritative World VM is untouched
  and the `--mpai` hashes were unchanged.
- **Byte-exact refactors.** One match-setup builder and one GameOptions /
  SlotInfo codec (`match_codec_test` pins goldens captured from the hand-written
  copies; replays and wire frames identical), the live TickBundle decoder kept
  lenient (strict only for replay files), the debug harness moved out of Release,
  and the client's const `world_` view. None changes `kNetVersion` or hashed
  state.
- **Client-only changes:** the information bar's multi-selection strip, group
  totals, squad label and centred status text; the solid green squad marker;
  three-way Texture Filtering; reclaim and Revive picking; override cache
  saving; menu launch-failure routing and dead local-server detection.
- **Cartographer:** symmetry tools, terrain clipboard, grouped section browser,
  wider built-in font and the per-concern source split (headless `--shot`,
  `--save` and `--bundle` outputs byte-identical across the split).
- **Platform and CI:** macOS Developer ID signing, notarization and stapling in
  a separate `sign` job; ccache for Windows, Linux and macOS; superseded cache
  pruning.

Protocol 256 is required for live play. Replays need the exact simulation
protocol, so protocol-253 (0.7.28) recordings are not playable by this build.
Replay format 12, generator version 8, campaign payload 4 and database schema
9 are unchanged since 0.7.28 (checked in source against `v0.7.28`: `git diff`
touches none of `src/net/replayhdr.h`, `src/tnt/`, `src/campaign/`,
`src/net/crusades.h` or the Crusades store).

## Local preparation checks

Pending.

## Remote multiplayer sweep

Pending.

## CI and packages

Pending. To check at the tag in addition to the usual list: the macOS ZIP,
debug ZIP and DMG attached to the release are the signed set (Developer ID
signature, stapled notarization ticket on both apps and the DMG, `spctl`
accepts them), not the unsigned build-job files.

## Screenshots

Done on 2026-10-12 with the 0.8.0 release-preparation build (`takclient
--version` 0.8.0; Debug with `-O2`, OpenGL) and a capture-only patch that is
not committed. Each new or re-rendered image was inspected by eye against its
caption.

- **Gallery.** Re-rendered: `title.jpg` (shows VERSION 0.8.0); `hud.jpg`, now a
  mixed 45-unit formation selection showing the information bar's portrait
  strip, FORMATION 1, 45 SELECTED and solid green `1F` markers (checked
  after 8deab7c6: a re-render's information bar is pixel-identical, since its
  eight types already fit at full size); `flyers.jpg`
  (Legion flyers over the front of the formation, protocol 254); and
  `cartographer.png` (the menu bar's new Symmetry menu). The other gallery
  images are unchanged; none shows the Options screen, a selection, a squad
  marker or the Cartographer.
- **User guide.** New: `guide/selection-strip.jpg`, the information bar of a
  hurt five-type group (GROUP 3, 24 SELECTED) at UI Scale 150%, re-rendered
  after 8deab7c6 so the portraits are full size out over the end-cap scroll, in
  [Selection](user-guide.md#selection); and `guide/cartographer-terrain.jpg`,
  the grouped terrain browser, in [Basics](user-guide.md#basics).
  Re-rendered: `guide/hud-annotated.jpg` and the five `guide/side-*.jpg` (status
  text now centred between the bar pairs), `guide/shift-queue.jpg`,
  `guide/flyers-retail-legion.jpg` (Legion flyers now over the leading ground
  units) and `guide/options-graphics.jpg` (Texture Filtering slider in place of
  Bilinear Filtering).
- **Capture fix.** The capture patch's spawned Legion scene now turns on the
  path service, as match setup does; without it the spawned Legion formation
  had no flyer stations.
- **Labels.** Development scenes and the selection still life are identified in
  their captions and in [the capture notes](img/README.md).
- **Size.** Total tracked image bytes rose from 15.78 MB to 15.96 MB (+0.18 MB,
  mostly the two new pictures).
- **Links.** All relative links, anchors and image paths in the changed
  Markdown resolve.
- **Not captured.** Texture Filtering at Units or Everything (every capture
  profile uses the Off default), the fallback stone information bar, the
  **+K** overflow tile, the Cartographer's symmetry axis overlay, and the macOS
  first launch.

## Limits

Pending.
