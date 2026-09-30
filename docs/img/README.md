# README screenshots

Captured on 2026-09-29 from the **0.7.14** release-preparation build, based on engine commit `2fa44e8`.
All nine game scenes were freshly captured before tagging and visually inspected.
Images are actual engine output at 1600 × 1000, encoded as JPEG at quality 90
with full chroma resolution. No units, effects, or UI were added afterward.

| Image | Scene |
| --- | --- |
| `title.jpg` | Title menu showing version 0.7.14 |
| `army.jpg` | Part of the Aramon army in the 16,000-unit local patrol fixture on Ulasem Arena, one simulated second in, 0.9× zoom; fog disabled; no AI or combat |
| `gameplay.jpg` | Aramon base in the local development demo on Ulasem Arena, one simulated second in; camera follows the selected barracks at 2× zoom |
| `naval.jpg` | Development naval demo on Cairbray Coast Landing, eight simulated seconds in; ships on water |
| `lobby.jpg` | Skirmish creation with Ulasem Arena preview and Allow Speed Change; live local-server connection |
| `campaign.jpg` | Book of Darien campaign picker with a fresh progress profile |
| `streaming.jpg` | Streaming setup with Max 3840 / 60 FPS defaults; no key or live broadcast |
| `diplomacy.jpg` | Built-in input-test fixture on Ulasem Arena after gifting: ally mana and enemy chat unchecked, monarch remains selected, so gift buttons are disabled; fog disabled |
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

The army capture uses `TAK_PATROL_PERF=1 TAK_PATROL_PERF_ZOOM=0.9`,
`--time 1 --nofog`, and the same renderer/preferences as the other game shots.
The 16,000 count is for the entire fixture, not the number visible in the image.

## Cartographer screenshots for 0.7.14

`cartographer.png` and `cartographer-model.png` were freshly captured from the
same 0.7.14 release-preparation source. Both are unmodified 921 × 691 PNGs of
Ulasem Arena using the actual editor and the user’s retail data.

- `cartographer.png`: Units browser, map canvas, feature art and minimap. A
  scratch SDL harness sends Tab key events and waits for background artwork.
- `cartographer-model.png`: F6 model inspector showing an Aramon Archer.
  The editor workflow test captures after asynchronous model/texture loading.

Both were visually inspected. The editor captures use SDL’s dummy video driver
and software renderer. No geometry, units, effects or UI were added afterward.
All eleven gallery files were refreshed for this release.

The campaign, match-creation and sample-results recaptures are byte-identical
to their previous gallery files: those static screens did not change. They were
recaptured and inspected alongside the eight images with new rendered output.
