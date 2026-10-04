# README screenshots

Captured on 2026-10-04 from the **0.7.23** release-preparation build. All thirteen views were freshly rendered and visually
inspected. Game/menu images are actual engine output at 1600 × 1000, encoded as
JPEG at quality 90 with full chroma resolution. Editor images retain their
native 921 × 691 PNG resolution. No units, effects or UI were added afterward.

| Image | Scene |
| --- | --- |
| `title.jpg` | Title menu showing version 0.7.23 |
| `army.jpg` | Part of the Aramon army in the 16,000-unit local patrol fixture on Ulasem Arena, one simulated second in, 0.9× zoom; fog disabled; no AI or combat |
| `gameplay.jpg` | Aramon base in the local development demo on Ulasem Arena, one simulated second in; camera follows the selected barracks at 2× zoom |
| `naval.jpg` | Development naval demo on Cairbray Coast Landing, eight simulated seconds in; ships on water |
| `lobby.jpg` | Skirmish creation with Ulasem Arena preview, Retail/Flowfield choice and Allow Speed Change; live local-server connection |
| `room.jpg` | Live local-server skirmish lobby with three Normal AIs, remembered example pack selections, and read-only rules with Flowfield selected; example pack folders are empty UI fixtures |
| `campaign.jpg` | Book of Darien campaign picker with a fresh progress profile |
| `crusades.jpg` | Live authenticated local Darien Crusades server; imported 313-territory definition with Torcairn selected, no authored battle maps or inferred ownership |
| `streaming.jpg` | Streaming setup with Max 3840 / 60 FPS defaults; no key or live broadcast |
| `diplomacy.jpg` | Built-in input-test fixture on Ulasem Arena after gifting: ally mana and enemy chat unchecked, monarch remains selected, so gift buttons are disabled; fog disabled |
| `results.jpg` | Results layout with faction/player-color emblems and the built-in five-player sample-statistics fixture, not a completed match |

Captures use isolated SDL preference directories (`XDG_DATA_HOME` and
`XDG_CONFIG_HOME`), windowed settings, shadows and the application defaults for graphics: AA,
bilinear filtering, smooth GUI art and smooth movies are off. Tree sway is on. Gameplay statistics are hidden. Screenshot
timing is not a performance measurement; the 16,000 count describes the whole
army fixture, rather than the visible unit count.

The Debug client's `--shot` normally chooses software rendering. Game/menu
captures explicitly use `SDL_RENDER_DRIVER=opengl` with the offscreen video
driver; their logs confirm OpenGL rendering. Setup uses a scratch SDL harness
connected to a temporary local server and renders the actual GameView directly.
Crusades uses the actual client connected to an authenticated temporary server
with an imported Darien definition, without authored battle maps or ownership.
The streaming panel has no key or live broadcast.

Cartographer captures use SDL's dummy video driver and software renderer. The
units-browser harness sends Tab events and waits for background artwork; the
F6 model-inspector capture uses the editor workflow test after asynchronous
model and texture loading.

Only image encoding changed after capture. Some static views may remain pixel-identical despite being freshly captured.
When refreshing the gallery, rerender and inspect every view, update these notes
and README captions, and keep retail archives, extracted assets and temporary
capture files out of Git.
