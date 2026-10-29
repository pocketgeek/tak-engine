# Height rendering and fog of war

TA:Kingdoms terrain is a **flat tile mosaic** — cliff/slope relief is painted
into the 32px tile art, and the heightmap drives gameplay (nav) plus a **per-unit
screen lift**, *not* geometric terrain displacement. This matches the retail
engine (KINGDOMS.icd: flat-tile software renderer + vertical unit lift, no
terrain skew). Everything below lives in the client's `GameView`
(`src/client/gameview*.{h,cpp}`) unless noted.

## Unit lift

- `heightAbove(wx, wz)` = bilinear heightmap sample minus the map's **modal**
  height (`heightRef_`), clamped ≥ 0. The modal reference means a uniformly-high
  map lifts nothing.
- `terrainLift = heightAbove * kHeightScale_` (screen-Y, toward north) and
  `terrainLiftX = heightAbove * kHeightScaleX_` (screen-X). The 2.5D view is
  tilted diagonally, so height could displace both axes. Current constants:
  `kHeightScale_ = 0.5` (retail's: its screen projection at `0x426820` forms
  `screenY = (z << 4) - cameraY - (height >> 1)`, confirmed under emulation;
  shared by the seat lift, wall occlusion, and picking — an earlier art-calibrated
  1.1 lifted units 2.2× too far) and `kHeightScaleX_ = 0` (horizontal **off**:
  retail's screen X has no height term at all). Live-tunable via `TAK_HSCALE` /
  `TAK_HSCALEX` (debug builds).
- Applied to **every height-anchored draw** of a mobile unit: sprite, shadow,
  HP bars, selection brackets, order rings, build ghosts, projectiles,
  particles, effects.
- **Buildings are exempt.** Use `isStructure(type) = type->maxVel <= 0` (the FBI
  `canmove` flag is unreliable — the Keep sets `canmove=1` with no velocity). A
  building sits flat on its footprint; `uLiftX(u)` / `uLiftY(u)` return 0 for
  structures. (Mana-deposit cells carry a large heightmap value while their
  medallion — a feature — draws unlifted, so lifting a lodestone there would float
  it off its base.)

## Wall occlusion

A unit on flat ground *behind* a wall is placed correctly, but the wall's baked
relief art projects up-and-north over that ground, and terrain paints before
units — so the unit would draw *over* the wall. `wallOcclusionY` scans up to
`kOccScan_` (12) cells south (toward the camera) for the tallest blocking wall
and clips the unit's model above that line, redrawing the hidden part as a faint
team-tinted silhouette so the unit is never lost. `World::setTerrain`
(`src/sim/sim.cpp`) also blocks the occluded ground in the nav grids so units
settle in *front* of walls; flat wall tops have no higher cell to their south, so
they stay walkable ramparts.

## Height-aware picking (screen ↔ world)

Because units render lifted, **every** screen↔world interaction must invert or
apply the lift — a flat `offX + sx/zoom` map lands on the wrong cell:

- `pickWorld(sx, sy)` — screen→world. Marches down the lift to return the
  front-most surface whose lifted position is under the cursor. Use for
  move/attack orders, click-select, and **placement/building** (conjuring onto
  elevated ground otherwise drops the unit on the low cell behind the rise).
- `unitScreen(u)` — world→screen of a unit's drawn body, including flyer cruise
  altitude. Use for **marquee/click selection** so a lifted or airborne unit
  (e.g. the flying Monarch) is picked where it is drawn.

## Fog of war

The display shows retail's own fog (KINGDOMS.icd; addresses and the
evidence in `docs/retail-engine.md`, "Fog of war display"). Retail keeps **one
sight plane per player** and both its game logic and its screen read it; so
do we. The simulation maintains it for every player (`World::viewCounts_`,
the view counts that `0x4c6800` stamps, plus the persistent Mapping bits
`navigationExplored_`), and those are hashed gameplay state. The client only
reads them: nothing below runs in the simulation, and the headless referee
draws nothing.

- **The plane.** 32 px cells of the *projected* map, (x, z - y/2): row `r`
  of the plane is row `r` of the tile art. A unit's footprint is centred on its
  world cell (x>>5, z>>5) and admits a cell when it lies inside twice the
  sight distance and below a cone from the eye (ground or flight height,
  floored at the sea, plus `sightheight`) through a coarse projected height
  plane: about 1x sight on flat ground, up to 2x from high ground over low
  ground, less looking uphill. A dying human player's unit keeps its footprint
  60 ticks. `retailSightFootprint` / `retailSightAdmits`
  (`src/sim/retailexploration.h`).
- **The local view** (`client/retailfog.h`, `GameView::captureFog`, every
  captured frame on the sim thread): a cell is *unexplored* where the local
  player's Mapping bit is clear (all explored under Map Revealed / +Mapping),
  *in view* where the count of the local player or an ally is nonzero (every
  explored cell under LOS off), otherwise *explored*. Only the 16 x 16-cell
  squares the World marked since the last capture are rebuilt
  (`World::viewDirty`, unhashed bookkeeping set where a footprint is added,
  moved or removed): the largest maps have a million cells. Before the first tick
  (the frame behind a campaign briefing) the client stamps the same footprints
  itself, since the World's first pass runs inside tick 0.
- **Units.** Another side's unit is drawn, picked, given a health bar, a
  Tactical Dot or a hidden-unit dot only when **LosOn** (`0x4f7210`) passes
  on that plane: any of its four projected bounding-box corners (x +/- 8
  footx, z +/- 8 footz, the far pair at y + model top) on a cell in view, never
  while cloaked or carried (`UnitR::losSeen`). Points (impacts, effects,
  features) test the projected cell of the ground under them.
- **Radar** never clears fog and never shows a unit in the main view. The
  minimap (`0x4f9970`) dots an uncloaked unit within radar distance of one of
  the local player's built, unlocked units (a circle on the 16.16 positions),
  otherwise one whose projected centre cell is in view (`UnitR::mapSeen`).
- **Overlay** (`GameView::drawFog`, retail's Glide path `0x498270`). Each cell
  is a byte, alpha in the high nibble and intensity in the low one: unexplored
  `0xf0`, explored `0x8f`, in view `0x0f`. Every cell becomes 2 x 2 ARGB4444
  texels blending it with its neighbours 19/5/5/3 of 32 (`0x498b60`), the
  texture is bilinear-filtered and alpha-blended, and it is modulated by the
  vertex colour: unexplored ground is opaque black, explored ground the
  side's `FogColor` (`gamedata/sidedata.tdf`; white for Aramon) at 8/15. It is
  drawn flat (no per-corner lift: the plane is already projected), offset by
  half the map's modal height like the unit lift, under a grid of quads
  anchored to the map (160 px, coarser far out; retail cuts the screen 6 x 6).
  **RollingFog** multiplies each vertex colour by
  `(sin(P/32*0.31 + 0.17t) * sin(X/32*0.11 - 0.4t) * sin(0.5t) + 1) / 2` and
  **WavyFog** moves the inner vertices by `10 * sin(..) * sin(..) * sin((t + 1.2) / 2)`
  px, with `t` the real clock. The texture is uploaded only for the 32-cell
  blocks a new plane changed (`client/fogupload.h`).
- **Minimap** (`0x4f93a0`): the same plane one texel per cell, unblended:
  unexplored black, explored darkened (retail remaps the pixel through a shade
  table; we draw half-opaque black), in view clear.

## Tuning knobs (env)

All of these are read only by debug builds; a release build uses the defaults.

| Var | Effect | Default |
| --- | --- | --- |
| `TAK_HSCALE` | unit lift per height unit (screen-Y) | 0.5 |
| `TAK_HSCALEX` | unit lift per height unit (screen-X) | 0 (off) |
| `TAK_FOG_STILL` | hold RollingFog and WavyFog at t = 0 | off |
| `TAK_FOG_TIME` | hold them at this many seconds (captures) | off |
| `TAK_FOG_NO_ROLLING` / `TAK_FOG_NO_WAVY` | turn either effect off | off |
| `TAK_FOG_DEBUG` | point-sample the overlay (retail's FogDebug) | off |
| `TAK_FOG_FULL_UPLOAD` | upload the whole overlay texture on every change | off |
| `TAK_HDEBUG` | enable the height debug overlay | off |
