# README screenshots

Captured on 2026-09-27 from TAK Engine **v0.7.8**, tagged at commit
`2276068`.
All seven scenes were freshly captured from the tagged build. The images are actual engine output at 1600 × 1000, encoded as JPEG at
quality 90 with full chroma resolution. No units, effects, or UI were added to
the images afterward.

| Image | Scene |
| --- | --- |
| `title.jpg` | Title menu showing version 0.7.8 |
| `gameplay.jpg` | Aramon base in the local development demo on Ulasem Arena, one simulated second in; camera zoomed to 2× |
| `naval.jpg` | Development naval demo on Cairbray Coast Landing, eight simulated seconds in; ships on water |
| `lobby.jpg` | Create-game screen with Ulasem Arena preview |
| `campaign.jpg` | Book of Darien campaign picker with a fresh progress profile |
| `streaming.jpg` | Streaming setup with the Max 3840 / 60 FPS defaults; no key or live broadcast |
| `results.jpg` | Post-game results layout rendered with the built-in sample-statistics fixture, not a completed match |

Captures used an isolated settings directory, smooth GUI art, bilinear
filtering, shadows, and 4× anti-aliasing. The base view uses the player camera;
the naval demo has fog disabled to show both fleets.

The debug client's `--shot` path normally selects SDL's software renderer.
These captures selected `SDL_RENDERER_ACCELERATED` at `SDL_CreateRenderer`
under GDB, with `SDL_VIDEODRIVER=offscreen`, so they use the normal OpenGL
rendering path, including shadow silhouettes. No source changes were needed.
The base capture also adjusted camera zoom and centering under GDB. Only the
screenshot encoding changed after capture.

When refreshing the gallery, capture the current build, inspect every image,
update these scene notes and the README captions, and remove unused images.
Keep retail archives, extracted assets, and temporary capture files out of Git.
