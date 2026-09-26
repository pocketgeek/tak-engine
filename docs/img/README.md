# README screenshots

Captured on 2026-09-26 from TAK Engine **v0.7.1** (commit `3764804`).
All five images are actual engine output at 1600 × 1000, encoded as JPEG at
quality 90 with full chroma resolution. No units, effects, or UI were added to
the images afterward.

| Image | Scene |
| --- | --- |
| `title.jpg` | Title menu showing version 0.7.1 |
| `gameplay.jpg` | Eight-AI benchmark, High intensity, Ulasem Arena; Zhon forces around 29 seconds |
| `naval.jpg` | Development naval demo on Cairbray Coast Landing, eight simulated seconds in; all ship spawn cells verified below sea level |
| `lobby.jpg` | Create-game screen with Ulasem Arena preview |
| `campaign.jpg` | Book of Darien campaign picker with a fresh progress profile |

Captures used an isolated settings directory, smooth GUI art, bilinear
filtering, shadows, and 4× anti-aliasing. The benchmark uses its ordinary
spectator view; the naval demo has fog disabled to show both fleets.

The debug client's `--shot` path normally selects SDL's software renderer.
These captures selected `SDL_RENDERER_ACCELERATED` at `SDL_CreateRenderer`
under GDB, with `SDL_VIDEODRIVER=offscreen`, so they use the normal OpenGL
rendering path, including shadow silhouettes. No source changes were needed.
Only the screenshot encoding changed after capture.

When refreshing the gallery, capture the current build, inspect every image,
update these scene notes and the README captions, and remove unused images.
Keep retail archives, extracted assets, and temporary capture files out of Git.
