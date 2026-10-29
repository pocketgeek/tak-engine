# TAK Engine 0.9.10

0.9.10 follows 0.9.9. A new **Hidden Units** option (Options → Graphics; not
in the original game) keeps units behind trees, rocks and buildings in sight:
by default the tree, rock or building in front of a unit fades out. Flat
scenery (stumps, crops, smudges, mana deposits) is now drawn under units, as in
the original game. On macOS, Auto now picks OpenGL, the faster renderer there,
and the Metal renderer no longer draws pieces that a clip should have removed.

**Compatibility first.** Every change in 0.9.10 is in the game's display and
options: the simulation, the network code, the server and the AI are
unchanged from 0.9.9, so 0.9.10 keeps network protocol **303** and replay
format **15**.

- **0.9.9 and 0.9.10 play together**, in either direction, as clients and as
  servers (checked in source against `v0.9.9`; no mixed-version game was
  run). A 0.9.10 player can join a 0.9.9 server and a 0.9.9 player a 0.9.10
  server. 0.9.8 and older still cannot (*protocol version mismatch*).
- **Replays:** 0.9.10 plays 0.9.9 replays, and 0.9.9 plays 0.9.10's. A replay
  recorded by 0.9.8 or earlier is refused with the version message, as in
  0.9.9.
- **Players:** nothing to do. Settings, your data folder choice, key bindings,
  saved `.kmp` maps, generated-map recipes and mod packs carry over. Hidden
  Units starts at **Fade**; set it to **Off** for the original look. On a Mac,
  a Renderer you chose yourself (Metal, say) is kept; Auto now starts OpenGL.
- **Server operators:** nothing to do. A 0.9.9 `takserver` serves 0.9.10
  players; update it when convenient (see
  [public-server.md](public-server.md#versions)).

## Hidden Units

Options → Graphics → **Hidden Units**: **Off**, **Dots** or **Fade** (the
default), with **Fade Opacity** below it. Not in the original game, which
shows a unit behind a tree or a building in no way at all (`docs/retail-engine.md`);
**Off** is the original look.

- **Which units.** Only units you would see anyway: your own, your allies' and
  enemies you can currently see. Never anything under fog, and never an enemy
  that is cloaked out of your sight. Only ground units: flyers are drawn over
  everything already, and another unit in front never counts as cover. Nothing
  is added while Tactical Dots is showing.
- **Fade.** A tree, rock or building whose drawn pixels reach one of those
  units is drawn whole at the **Fade Opacity** setting (0 to 100%, default
  65%; 100% is no fade, 0% gone), so the unit, its shadow and the ground show
  through. It fades out over 200 ms and back in over 350 ms after the last
  unit leaves. A building being conjured or clipped by a terrain wall fades
  too; a placed site still waiting for its builder does not. The slider is
  greyed out unless Hidden Units is Fade, and both apply at once.
- **Dots.** A unit whose centre is covered shows its tactical dot (same size,
  colour and selection outline as Tactical Dots) on top of the cover; clicking
  the dot selects the unit. A unit whose centre is in sight gets none.
- **The same on every renderer.** Fade draws the cover translucent in the
  ordinary drawing pass; it needs no OpenGL-only path.
- **Cost.** Measured at 1280 x 960 (single runs): about +0.07 ms a frame in a
  forest and +0.57 ms with a 1,800-unit army in view; with **Off** a 10,000-unit
  view draws as fast as before, within measurement noise.

Saved in `settings.ini` as `hiddenUnits = off | dots | fade` and
`hiddenFadeOpacity = 0..100`. A settings file without them (0.9.9 or older)
gets Fade at 65%; an unknown value reads as Fade. Going back to 0.9.9, the two
keys are skipped and dropped at its next save.

## Flat scenery under units

Before, every feature sorted with the units by depth, so a stump or crop row
just south of a unit painted over its feet. The original game's world pass (`KINGDOMS.icd` `0x4fc648..0x4fcc46`, read by
static analysis; the engine copies none of the original's code or data) draws
every feature whose TDF `height` is below 10 in a cell scan **before any
unit**, then, row by 16-pixel row, that row's ground units followed by that
row's taller features, and the flyers last. There is no depth test between
sprites and models.

0.9.10 does the same for flat features: stumps, crops, smudges and mana
deposits are drawn before every unit, so they never cover anyone. Taller
features (trees, rocks, henge stones) still sort with the ground units by
depth. The old nudge that kept a mana deposit's decal under its lodestone is
gone; the new order does that by itself. The draw order is written up in
`docs/retail-engine.md`, which no longer says that retail z-buffers between
sprites.

## macOS: Auto picks OpenGL

On Windows (since 0.9.1) and Linux, Auto starts OpenGL first, because some
of the game's fast drawing paths exist only for OpenGL. macOS now does the
same: Auto tries OpenGL, passing over Apple's software OpenGL ("Apple Software
Renderer"), then Metal, then SDL's own choice. In a quick test on a MacBook
Neo, OpenGL drew a 2,000-unit view 2.6 times as fast as Metal; on the Neo, Auto
chose OpenGL ("Apple A18 Pro").

A Renderer you set yourself is used as it is. If OpenGL misbehaves on your Mac
(wrong colours, flicker, a crash, a soft-looking picture), choose **Metal**
under Options → Graphics → Renderer, what 0.9.9 and earlier used, and restart;
if the game does not reach the menu, set `renderer = metal` in `settings.ini`
(see the [user guide](user-guide.md#renderer)).

## Metal: clipped drawing stays clipped

SDL 2.32.10's Metal renderer keeps the previous clip (scissor) when a new clip
rectangle covers no pixel of the screen, so drawing meant to be clipped away
completely landed on screen instead. The game now skips such draws itself
(`src/client/cliprect.h` works out the clip's pixels as SDL does). Seen as:

- a unit behind a terrain wall at the edge of the view drawing its hidden part
  on Metal;
- the chat box;
- the Options and Hotkeys lists, and their dropdowns, in a window too small for
  them: there SDL turned clipping off on **every** renderer, so the lists drew
  outside their frame.

## Smaller changes

- **Debug builds:** `takclient game "<map>" --data <dir>` without `--server`
  opens the single-player room and waits there; `TAK_SP_AIS=N` seats N AI
  opponents and starts at once. The debug `--help` says so, and
  [development.md](development.md) has an example. Release builds are
  unchanged.
- **Options picture** in the user guide retaken with the Hidden Units and Fade
  Opacity rows.

## For modders and mapmakers

No file format changed. A feature's TDF `height` now decides how it is drawn,
as in the original game: below 10 it is flat scenery, drawn under every unit
and never faded by Hidden Units; 10 or more it sorts with the units and can
fade. Give a feature a real height if units should be able to stand behind it.

## For contributors

- All changes are in `src/client/` (and tests and docs): `git diff v0.9.9 --
  src/sim src/net src/ai src/server` is empty. No hashed state, no command,
  no protocol change.
- `src/client/hiddenunits.h` (policy: modes and their parsing, painter layers
  with `kLayerLowFeature` for height < 10, cover tests, fade timing) and
  `src/client/gameview_hidden.cpp` (per-frame cover search, Dots and Fade);
  `gameview_render.cpp` builds the painter list by layer, then map z.
  `src/client/cliprect.h` (a clip's output pixels, as SDL computes them);
  `renderdriver.h` `autoOrder` for macOS (OpenGL, Metal).
- Settings `hiddenUnits` (saved by name) and `hiddenFadeOpacity`.
- Tests: new `hidden_units_policy` (`tools/hidden_units_test.cpp`) and the
  data-backed `hidden_units` (Ulasem Arena, `--testbuild`: flat features under
  every unit, Dots' cover rules and click target, Fade's covers and that 100%
  equals Off); `renderdriver_test` macOS Auto order and the Apple software
  renderer; `settings_test` Hidden Units and Fade Opacity.
- `--mpai` Inner Circle seed 1, `TAK_SPEED=80`: unchanged, as no simulation
  code changed (Retail AI 60 s `4952493f51b9a568`; the others as in 0.9.9).
  The cross-compiler golden (`check-determinism`) is expected unchanged,
  `dcef618cd2e4d558`. See the [validation report](release-0.9.10-validation.md).

## Known limits

New with 0.9.10:

- **Fade's automated test** does not cover a swaying tree or a building drawn
  as its distant image fading; those code paths exist and were reviewed, but
  only seen in play.
- **A faded building can show a faint line** where its own faces overlap.
- **Dense forests:** one unit can fade two or three overlapping tree crowns.
- **Snapping back:** switching Hidden Units away from Fade, or a frame in
  which Tactical Dots shows, puts faded covers back at once instead of fading
  them in.
- **Retina with OpenGL:** on a Mac's built-in Retina panel, the sharpness of
  the OpenGL picture has not yet been looked at by a person. If it looks soft,
  choose **Metal**.
- **No mixed-version game** of 0.9.9 and 0.9.10 was run; that they play
  together is checked in source (below).
- **0.9.9's limits still apply:** see the
  [0.9.9 notes](release-0.9.9-notes.md#known-limits), and through them the
  earlier notes.

## Compatibility

Protocol **303**, replay format **15** and generator version **9**, all
unchanged since 0.9.9 (checked in source against `v0.9.9`: every source change
is in `src/client/`, and none touches the simulation, the network code, the
server or the AI). A server accepts a client whose protocol and original game
files match its own and does not compare release numbers, so **0.9.9 and
0.9.10 clients and servers can play together** in either direction. Replays
from 0.9.9 play in 0.9.10 and the other way round. Saved `.kmp` maps,
generated-map recipes, pack folders and settings carry over; the two new
settings are skipped by 0.9.9 if you go back, and dropped from the file at its
next save.

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.9.10-validation.md).
