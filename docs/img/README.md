# README screenshots

Captured on 2026-09-29 from the **v0.7.11** source, tagged commit `eea2b30`.
The setup capture was taken before tagging from the same engine code; the other
six scenes were captured after rebuilding the tagged source.
All seven scenes were freshly captured and visually inspected. Images are actual
engine output at 1600 × 1000, encoded as JPEG at quality 90 with full chroma
resolution. No units, effects, or UI were added afterward.

| Image | Scene |
| --- | --- |
| `title.jpg` | Title menu showing version 0.7.11 |
| `gameplay.jpg` | Aramon base in the local development demo on Ulasem Arena, one simulated second in; camera follows the selected barracks at 2× zoom |
| `naval.jpg` | Development naval demo on Cairbray Coast Landing, eight simulated seconds in; ships on water |
| `lobby.jpg` | Skirmish creation with Ulasem Arena preview and Allow Speed Change; live local-server connection |
| `campaign.jpg` | Book of Darien campaign picker with a fresh progress profile |
| `streaming.jpg` | Streaming setup with Max 3840 / 60 FPS defaults; no key or live broadcast |
| `results.jpg` | Results layout with the built-in sample-statistics fixture, not a completed match |

Captures used isolated SDL preference directories (`XDG_DATA_HOME` as well as
`XDG_CONFIG_HOME`). Menu/game captures enabled smooth GUI art, bilinear filtering,
shadows, and 4× anti-aliasing. Gameplay statistics are hidden; screenshot timing
is not a performance measurement. The naval demo disables fog to show both fleets.

The debug client's `--shot` normally chooses software rendering. These captures
set `SDL_RENDER_DRIVER=opengl` with `SDL_VIDEODRIVER=offscreen`; logs confirm the
OpenGL renderer, including shadow silhouettes. No source changes or GDB were
needed. The setup capture uses the real GameView through a scratch SDL harness
connected to a temporary local server. Only encoding changed after capture.

When refreshing the gallery, capture the current build, inspect every image,
update these scene notes and README captions, and remove unused images. Keep
retail archives, extracted assets, and temporary capture files out of Git.
