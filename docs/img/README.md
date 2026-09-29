# README screenshots

Captured on 2026-09-29 from the **v0.7.12** source, tagged commit `7d463ee`.
All nine scenes were freshly captured after tagging and visually inspected.
Images are actual engine output at 1600 × 1000, encoded as JPEG at quality 90
with full chroma resolution. No units, effects, or UI were added afterward.

| Image | Scene |
| --- | --- |
| `title.jpg` | Title menu showing version 0.7.12 |
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

## Cartographer screenshots for 0.7.13

`cartographer.png` and `cartographer-model.png` were freshly captured on
2026-09-29 from the 0.7.13 release source (`af7f653`, before the version-only
release stamp). Both are unmodified 921 × 691 PNG captures of the actual editor.
The map is Ulasem Arena using the user's retail data.

- `cartographer.png`: Units browser, map canvas, feature art and minimap. A
  scratch capture harness sends the same Tab events as the keyboard and waits
  for background art/minimap work before capturing the live editor renderer.
- `cartographer-model.png`: F6 model inspector showing an Aramon Archer from
  the unit browser. Captured through the editor's model workflow test after
  asynchronous model/texture loading. This is a visual inspector, not a combat
  scene. The capture uses SDL's dummy video driver/software renderer.

Both images were visually inspected. No asset geometry, units or UI were added
after capture. The 0.7.12 game gallery above remains representative and has not
been relabeled as newly captured.
