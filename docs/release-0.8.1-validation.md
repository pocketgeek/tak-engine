# 0.8.1 release validation

Protocol **256**, replay format **12**, generator version **8**, campaign
payload **4**, database schema **9** (all unchanged since 0.8.0).

Not yet tagged.

## Scope and compatibility

0.8.1 follows 0.8.0 (protocol 256) and carries no protocol change. After the
0.8.0 validation commit `04a122eb`, the 17 commits `adcb8fdb`..`e5dc92cf` change
only `src/client/`, the client's tests, CMake and documentation:

- **Information bar declutter** (`adcb8fdb`, `12e19f16`). Each element keeps its
  `.gui` spot but draws only with data: the portrait and left HP bar with a
  selection, the left mana bar for a pool, the right name and progress bar only
  with a conjure target; the right mana bar is removed. The first portrait of a
  multi-selection is one slot of the type strip (`selportraits::layout` counts its
  spot in the strip's span). `selportraits_test` checks slot 0 matches its
  neighbours in every layout state. Documented as a deliberate departure from
  retail in `docs/retail-engine.md`.
- **GPU memory** (`83059faa`, `72e68723`, `9360e477`, `e271ba6c`, `8d0416ec`,
  `58a8b026`, `cd817279`, `d9920b85`, `3848cbce`, `244e0776`, `a994f1d1`).
  `gpuvram` post-failure tightening now relaxes after 60 s without a failure,
  doubling per re-tightening up to 8 minutes (`gpuvrampolicy.h`, driven by
  `gpuvram_test` with a fake clock); a settable limit with the Options GPU MEMORY
  LIMIT choice (Auto = 1280 MiB, 384 MiB floor), persisted as `gpuMemoryLimit`;
  terrain fallback sections uploaded on demand with a 64 MiB idle budget and 3 s
  expiry instead of all at load; cached chunks at another mip level stand in while
  the wanted level bakes; under the cap a needed section sheds off-screen chunks;
  colour atlases are never refused (the terrain caches shed first); lightning,
  the Crusades fallback font and campaign map, and the stream capture PBOs and
  renderbuffer are counted. `terrain_cache_test`, `settings_test` and
  `override_settings_test` cover these. Measured peaks are in the
  [release notes](release-0.8.1-notes.md).
- **Options dropdowns** (`910ee228`, `8d0416ec`): open on the side with more room,
  whole rows only, scroll thumb when too long; label column widens to fit.
- **Streaming Remember Key** (`43631028`, `e5dc92cf`): `src/client/credstore.h`
  with one backend per platform (Windows Credential Manager via advapi32, macOS
  Keychain via the Security framework, Linux Secret Service through libsecret
  loaded with `dlopen`, none elsewhere), all store calls on one worker thread
  (`StreamKeyMemory`); only `streamRememberKey` is a setting. `stream_key_test`
  (fake store) and `credstore_test` (real store under a test-only name, exit 77
  when there is none) are new.

**Compatibility.** `kNetVersion` is 256 in `src/net/protocol.h` at `e5dc92cf`,
as at `v0.8.0`. `git diff v0.8.0..e5dc92cf` touches nothing under `src/sim/`,
`src/net/`, `src/server/`, `src/ai/`, `src/campaign/`, `src/tnt/` or the Crusades
store, so replay format 12 (`kReplayFormat`), generator version 8, campaign
payload 4 (`src/net/crusades.h`) and schema 9 (`kSchemaVersion`) are unchanged
and no hashed simulation state can have moved. The server's Hello check
(`Server::handshake`) rejects only a different `kNetVersion` or a different
pure-retail gameplay-data hash; the client sends an empty build id and the server
does not compare engine versions. So **0.8.0 and 0.8.1 clients and servers can
play together in either direction**, and replay playback, which requires
`protocol == kNetVersion` (`supportedReplayProtocol`), accepts 0.8.0 recordings
in 0.8.1 and the reverse. The replay header's `engineVersion` string is
informational. A 0.8.0 build reading a 0.8.1 settings file skips the two new keys
(unknown keys are ignored on read) and drops them on its next save.

`kNetVersion` must be re-checked immediately before tagging (in 0.8.0 the
protocol moved during preparation).

## Local preparation checks

Pending.

## Remote multiplayer sweep

Pending. In addition to the usual sweep, a mixed-version check (a 0.8.0 client
against a 0.8.1 referee and the reverse) would confirm the compatibility claim
above in practice.

## CI and packages

Pending. To check at the tag in addition to the usual list: the Linux packages
gain no libsecret dependency (it is loaded at run time), the Windows binaries
import nothing new beyond `advapi32.dll`, and the macOS sign job still signs,
notarizes and staples both apps and the DMG.

## Screenshots

Done on 2026-10-13 with the 0.8.1 release-preparation build (`takclient
--version` 0.8.1, build `v0.8.0-18-ge5dc92cfd49b-dirty` from the version bump and
the capture patch; Debug with `-O2`, OpenGL) and a capture-only patch that is not
committed. Each re-rendered image was inspected by eye against its caption.

- **Why so many.** The information bar change made every picture that shows the
  bar stale: with nothing selected the old pictures showed an empty black
  portrait box and empty bar outlines, and with a selection an empty right-hand
  bar. `12e19f16` had fixed `guide/hud-annotated.jpg` by covering the gauges with
  neighbouring bar art and `guide/selection-strip.jpg` by cropping; both are now
  real re-renders.
- **Gallery.** Re-rendered: `title.jpg` (shows VERSION 0.8.1); `hud.jpg` (the
  45-unit formation selection, no right-hand bar; its stats panel's VID row reads
  87 MB, against 151 MB in the 0.8.0 capture); `flyers.jpg` and `naval.jpg` (empty
  information bar); `diplomacy.jpg` (Elsin's bar without the right-hand bars; the
  fixture now renders fog of war, which the 0.7.25 picture did not show). The
  capture patch's larger naval fleet, dropped after 0.7.26, was added back.
  `streaming.jpg` already shows **REMEMBER KEY** (rendered for `43631028`) and was
  kept. `battle.jpg`, `dragons.jpg` and `generated.jpg` are spectator views without
  the bar; the menu pictures and the Cartographer pictures are unaffected.
- **User guide.** Re-rendered: `guide/hud-annotated.jpg` (same eight markers),
  `guide/selection-strip.jpg` (same 1160 × 110 crop), the five `guide/side-*.jpg`,
  `guide/shift-queue.jpg`, `guide/flyers-retail-legion.jpg`,
  `guide/fire-on-the-move.jpg` (now the 0.8.0 portrait strip, 2 Mage Builders and
  9 Archers, 11 SELECTED, instead of the 0.7.28 single portrait),
  `guide/campaign-briefing.jpg`, `guide/replay.jpg` (a fresh recording of the same
  scripted Legion move, played back by the same build) and
  `guide/options-graphics.jpg` (scrolled one step further so the whole Graphics
  section shows, ending with **GPU MEMORY LIMIT** at AUTO (1.25 GB)).
  `guide/benchmark.jpg`, `guide/scorecard.jpg` and `guide/tactical-dots.jpg` are
  spectator views and were kept; `guide/cartographer-terrain.jpg` is unaffected.
- **Labels.** Development scenes and the selection still life are identified in
  their captions and in [the capture notes](img/README.md).
- **Size.** Total tracked image bytes fell from 15.90 MB to 15.77 MB (−0.13 MB).
- **Links.** All relative links, anchors and image paths in the changed
  Markdown resolve.
- **Not captured.** The GPU Memory Limit dropdown open, **LOWERED FOR NOW**,
  Remember Key ticked or greyed out (**NO SYSTEM PASSWORD STORE**), a
  conjure target's right-hand bar, the **+K** overflow tile, and Windows and
  macOS rendering.

## Limits

Pending.
