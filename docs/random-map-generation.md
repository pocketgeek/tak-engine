# Random map generation

The engine uses generator version 10. The map ID carries the seed, optional
title and parameters in `~gen1~` followed by hexadecimal bytes; the first two
payload bytes identify the generator version. Three bytes, the **Plateaus**,
**Mountains** and **Towns** amounts, follow the scattered-mana level and come
before the title; they control the retail terrain kits described below. With all
three at zero the map has none of those kits.

Only version 10 is a valid recipe: an ID with any other version, or a payload
that is not exactly the 26 fixed bytes plus a title of at most 24, is malformed.
A server refuses a game room for a malformed ID ("unknown random-map recipe ...;
generate a new map"), and nothing builds or saves a map from one; a game room
whose remembered map is such a recipe opens on another map and says why, and the
CHANGE MAP panel's RANDOM MAP tab starts from the default settings instead. A saved
generated `.kmp` is an ordinary map and plays from its saved terrain whatever
recipe it records. Clients and servers must run the same engine version. The same
version-10 recipe built different terrain before protocol 327 (compact mountain peaks,
below), so a recipe saved earlier may now come out with mountains it lacked.

## Layout and placement

- **Mainland:** noise-shaped coasts with connected starting areas and broad,
  reserved ground routes toward the interior.
- **Lakes:** plentiful water and connected starting areas; water can reach map edges.
- **Islands:** separated home islands, connected sea lanes, and open water for
  naval construction and ship departure. Travel between islands needs boats or
  aircraft. Water allocation is automatic.
- **Taros Maze:** seeded branching corridors between retail cliff walls. Home
  clearings join the maze; this is a traversable battlefield, not a puzzle with
  exactly one route. Water and standalone hill controls are automatic; the
  maze walls are its plateaus, and Taros mountains appear only where a wall
  block is wide enough to hold one.
- **Veruna Ports:** coastal settlements around a shared deep-water basin, with
  land routes around its perimeter. Every start has a validated harbor suitable
  for a Sea Fort and ship departure. Water allocation is automatic. These are
  terrain harbors, not prebuilt forts or decorative dock structures.
- **Aramon Riverlands:** branching waterways with reserved land crossings.
  Water allocation is automatic; crossings can divide navigable river reaches.
- **Zhon Clearings:** dense jungle stands surrounding home clearings and reserved
  army paths. The forest slider still controls the amount of added vegetation.
- **Creon Highlands:** broad authored hillsides and high ground around connected
  lowland routes. The Highlands slider (in place of Hills) controls coverage;
  water is disabled, and Plateaus does not apply. Creon mountains may stand on
  the highlands.
- **Volcano Lava Flows:** a lava river runs from a fifth of the way out to the map's
  edge between each pair of neighbouring homes, meandering and varying in width; the
  homes meet in the dry middle. Lava allocation is automatic.
- **Volcano Industrial Zone:** lava everywhere but the home plateaus and a few
  two-section plateau blocks out in it (at least one per player), joined by a maze
  of retail walkways, after the Industrial Zone v1/v2 community maps. A seeded
  spanning tree over one connected stretch of lava gets a few loops, and its
  branches are cut back to the plateau ramps (two per home, one per block), one in
  five left as a dead end. Each dead end finishes in a pad holding a sacred stone
  (neutral mana, reached along the walkways, in the shipped tier mix). Lava pads
  (*isle1–4*) dot the lava at the recipe's water amount (`waterDensity`/576 of the
  lava sections: one in six at the default 96, none at 0); nothing walks to them.
  Each pad holds a sacred stone too (`Result::padMana`, not counted as scattered
  mana), a bonus only flying builders claim, so the final reachability check leaves
  them out. Walkways,
  ramp landings and a lane from each start to its ramps are kept clear of ruins.

**Volcano** is Iron Plague's lava world (`kingdom=volcano`, the world of the
campaign's takx20/takx21). Its ground is the plating plateau of *flats/base01–03*
(mixed section by section) at height 220 over lava at 0, sea level 56; retail marks
every lava cell 0xfffc, so lava is impassable to everything but aircraft (no ships,
no harbors: it has no Islands layout). The lava rim is the *base* kit, named as
Zhon's coast. It has no cliffs, mountains or towns, no trees and no standing stones:
its deposits are bare, as the campaign's are, and Creon works, whole and ruined
(*CreInvent01–04*), stand in for rocks. On Mainland and Lakes the WATER slider reads
LAVA.

In the game room's CHANGE MAP panel (RANDOM MAP) each biome offers four layouts:
Mainland, Lakes, Islands (Industrial Zone on Volcano) and its own themed layout.
Cartographer lists the themed layouts by faction; selecting one sets its biome.

Width and height are set separately, each 8, 12, 16, 20, 24, 32, 40, 48, 56 or 64 sections
(one section is 32 simulation cells); the menu skips a size the layout cannot build with
the other side as it is, and `sanitize` grows a side that does not fit (the smaller side
first, along those steps). What fits was measured, not assumed: every layout, 2–8 players
and widths × heights of 8–64 sections were generated for 16 seeds (3,080 combinations),
and a size counts only when every seed built a map that keeps its layout (Maze walls and
open exits, Highlands uplands, water on Lakes, Riverlands and Ports, a harbor per player on
Islands and Ports). Two rules reproduce that with no size allowed that fails:

1. The start plan keeps every start 72 cells from the others for any of the eight
   rotations the seed picks (`startsSpacedForAnySeed`). This alone decides Mainland, Maze,
   Jungle and Highlands, and keeps 6–8 player maps at 12 sections or more on a side unless
   the other side is long.
2. A shortest side, in sections, by layout and players (held level or rising with players):

| Layout | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Mainland, Maze, Jungle, Highlands, Lava Flows | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
| Industrial Zone | 8 | 8 | 12 | 16 | 16 | 16 | 16 |
| Lakes | 8 | 8 | 8 | 12 | 12 | 12 | 12 |
| Riverlands | 8 | 8 | 12 | 12 | 12 | 12 | 16 |
| Islands | 8 | 12 | 12 | 16 | 16 | 20 | 20 |
| Ports | 12 | 12 | 16 | 16 | 16 | 16 | 16 |

Starts are planned before terrain is painted, with a minimum 72-cell separation,
level home areas, and clear 24×24-cell construction squares. Home economies are
reserved before hills, scenery, or coast sections can occupy them. Each start
receives the same three sacred-stone strengths at the same offsets. Additional
mana sites are admitted in complete rounds: equal counts and strengths, with
walking-distance bands of ±12 cells around the round's target. A candidate must
be closer by walking distance to its owner than to another reachable start.
If any player lacks space for another round or its surrounding ruins, the
entire round and its new route reservations are discarded for everyone.
This is an economic fairness rule, not a promise of perfectly symmetric terrain.

The generator uses the game's retail terrain classifier and footprint
clearance. It validates six-cell-wide ground bodies between mainland/lake starts
and each owner's mana sites, after placing scenery. Expansion approaches are
reserved along actual reachable routes. Island harbors share an eight-cell ship
component and have a clear 24×48-cell construction/departure area, exceeding the
Sea Fort's 6×18 yard. These checks do not replace the game's pathfinder.

## Appearance and controls

Coasts copy entire retail terrain sections: tiles, authored heights, and
features. Hills use standalone low-ground sections whose edges match the
surrounding elevation; their artwork and heights are copied together. Unsafe
or unavailable relief pieces are omitted, never replaced with invisible raised
ground. Available pieces and free space limit hill variety, particularly on
small islands with large reserved home areas. Maze and Highlands assemble
compatible Taros cliff and Creon hillside sections with their matching
high-ground fills. Every layout also assembles the retail
cliff/plateau, mountain and town kits (next section). All
section assets the generators read participate in the data hash.

### Retail terrain kits

**How retail builds them.** Every shipped multiplayer map is tiled entirely from
the 512-pixel sections in `sections.hpi` / `IPSections.hpi` (each section is one
terrain JPG). Matching every map block's JPG key back to its section shows how the
kits are used: *Low to High Cliffs* (and *Hillsides*) pieces ring a *High Flats*
fill, *High Mountains* stand on that plateau, and *Low/High Towns* are walled,
road-flagged settlements around a cobbled fill. Each kit is a marching-squares set
like the coast: a piece's role says which of its four corners lie inside the shape.
The generator reads them that way:

- **Plateau edges** are classified from their authored corner heights (the nearer
  of the kit's low and plateau level). An edge between two low corners must sit at
  the low level along its outer two rows, because it meets walkable ground; a mixed
  edge always meets another edge piece. Zhon's south-facing jungle cliffs stop a
  little short of the plateau level at their rim (retail lays them against the
  plateau fill all the same), so their role comes from the name (`1ne` outer,
  `2sw` inner, `rampe`). Dead ends, voided fills and Veruna's quarry pit are left out.
- **Ramps**: each straight edge piece is tested with the engine's own terrain
  classifier between flat low and high ground, with its sides blocked (in a rim its
  neighbours are cliffs). A ramp takes the best piece its rim role offers: a cliff
  the generator's army class (six cells wide, slope 16) can climb (Veruna, Zhon,
  Creon); else a cliff that builders and infantry (the game's GROUND3 class: three
  cells, slope 30) can climb (Aramon), so the rim stays one family; only then a
  hillside builders climb (Taros, whose hillsides are too steep for siege engines,
  as in retail). Retail's *Hillsides* interlock with each other, not with cliffs:
  their variants are end pieces, each joining a cliff cleanly on one side only. So
  Taros lays them as retail does, as runs with straight cliffs at both ends: `nnw`
  alone on a north rim, `sse`+`ssw` on a south rim, `ese` over `ene` on an east rim
  and `wsw` over `wnw` on a west rim. Every plateau gets 1–4 spaced ramps (one per
  ten plateau corners), the first facing the nearest start, each leading onto open
  plateau, never into a mountain. Hillsides are never plain rim.
- **Mountains** use the retail role names (their blockers are too ragged to
  classify). They are placed first, apart from the plain plateaus: a few seeds per
  map (at most one per 64 sections), each grown by a short four-way walk into a
  single peak, a ridge or a clump, and raised on a plateau of its own with a corner
  of open, walkable top around the range (two on Creon, for the snow line). The
  rings the range needs are whole; the outer margin is ragged, so mountain plateaus
  are irregular rather than square. When no range finds room (small or crowded
  maps, where the homes and the routes kept open between them leave no block that
  wide), a second pass places one compact peak: a single core needing only the moat
  of high ground round it (Maze and Highlands: of their own high ground), the corners
  beyond it clear of every home, its outer margin raised only where plateau may. Plain plateaus then fill the rest of the
  eligible ground, a corner clear of every mountain plateau: the Plateaus and
  Mountains sliders act independently, and Mountains 0 leaves the plain plateaus as
  they are.
- **Towns** are classified from their road-flagged corners, which must agree with
  the role name. Gate pieces are not used: their road runs out to the section edge
  and would stop dead in the grass. As in retail, the painted walls carry no
  blockers. Houses and other town features use their real TDF footprints on the
  paved inside, three cells clear of the wall band and of every reserved lane.

| World | Plateaus (edge kit, fill, levels) | Mountains | Towns |
| --- | --- | --- | --- |
| Aramon | *Low to High Cliffs* (its infantry-climbable variants are the ramps), *alightgr_200*, 80→200 | *High Mountains* (all 13 roles) | *Low Towns* on *cobb_80*; *High Towns* on *cobb_200* on plateaus; houses, wells, carts, fountain |
| Taros | *Low to High Cliffs* + *Hillsides* runs as ramps (builders and infantry), *high_ground*, 62→235 | *High Mountains* (13 roles) | *Low Towns* on *low cobbles*; *High Towns* on *high cobbles*; hovels, buildings, stocks, impalers |
| Veruna | *Low to High Cliffs* (its walkable variants are the ramps), *int_200*, 62→200 | **none**: Veruna ships no mountain kit; its *High Specials* are craters and outcrops, not blocking mountains | *Low Towns* on *s_cobbflat*; *High Towns* on *i_cobbflat*; houses, huts, wells, lamps, fountain |
| Zhon | *Low to High Cliffs Jungle*, *mountainplateau230*, 63→215 (ramps S/E/W) | standalone spires *High Specials/mt1–mt4*: Zhon's *High Mountains* (`mttp_`) are a ridge set crossing edge midpoints, not a marching kit | **villages**: Zhon has no settlement sections (*Low Plazas Jungle* are stepped temple platforms on a dirt palette that does not border the jungle fill), so a village is retail tents, huts and a bonfire in a clearing |
| Creon | *hillsides* (all walkable slopes), *highland512*, 80→250 | *mountains* (+ *intmt512* centre) on snow: the 256-pixel *snow line* kit blends half a section of *snow256* around each range into the highland | *low towns* on *chunglo*; *high towns* on *chunghi*; houses and civic buildings |
| Volcano | **none**: the plateau is the ground | **none** | **none** |

**Placement and fairness.** Plateau corners come from a low-frequency noise field and
are only allowed where no water corner touches them, more than 48 cells from every
start (so the 24×24 construction square and the home deposits stay on level low
ground) and more than 24 cells from every reserved army route; a lone high corner is
dropped. With Plateaus at 48 or more every start also gets a plain plateau of its own
three to twelve corners out, nearer to it than to any other start, so high ground
is shared out evenly rather than left to the noise. Maze and Highlands keep their
own high ground: their mountains go only where it already surrounds the core, and
the Plateaus amount does not apply. Towns take plain ground of one level (low, or a plateau top), at least a
section from other towns, outside every home economy; nothing else is placed in a
town. Plateau tops get trees and rocks from the same forest fields, and mana: from
the second extra-mana round on, a round first tries the kit plateau tops (every
round until one succeeds, then every other). It is all or nothing, under the same
rules as any round: every player needs a top site in one common ±12-cell walking-
distance band, measured for builders through the ramps, nearer by walk to that
player than to any other start, with the usual ruins on the top; otherwise (or if
the ruins find no room) the round falls back to the low ground. Scattered neutral
mana may also land on a top the army class can reach. In the sweep below, at the
default amounts, maps with plateaus got plateau mana in 13/24 2-player 16×16 maps,
23/24 at 24×24 and 22/24 at 32×32; 4-player maps about half to two-thirds of the
time from 24×24 up; 8-player maps rarely (all eight must find a fair top in one
band). Across all layouts and player counts, 26% of plateaus carried mana. The final
walking-distance validation applies as on any map and sees mountains, cliffs and houses
as the simulation will. If kit terrain leaves no room for a fair map, the generator retries
deterministically with half the kit amounts, then none (`Result::kitFallback`); a
sweep of 1,620 maps (five worlds; Mainland, Lakes and each themed layout; 2, 4 and 8
players; 16×16 to 32×32; two seeds; six Plateaus/Mountains combinations) never
needed it. At the defaults it held 679 plain and 245 mountain plateaus; Mountains
255 raised 705 mountain plateaus beside 536 plain ones, Mountains 0 kept 723 plain
plateaus, and Plateaus 0 left only the 245 mountain plateaus.

**Controls.** The CHANGE MAP panel's RANDOM MAP tab shows the nine sliders in two columns: Forests,
Rocks, Extra Mana, Scattered Mana, Water, Hills (Highlands on Creon Highlands),
Plateaus, Mountains and Towns (Villages on Zhon). Plateaus reads AUTO on Maze and
Highlands; Hills reads NONE on Veruna Ports (its land ring is too narrow beside the
kept-open routes for any hill section) and on Volcano, which also has no Forests (no
trees) and calls Rocks RUINS; Mountains reads NONE on Veruna and Volcano, and the recipe box reads
MOUNTAINS: NO ROOM when the previewed roll placed none (Cartographer's preview says
so too). Defaults: Plateaus 96, Mountains 48,
Towns 64 of 255: on a 24×24 two-player map about three plain plateaus (one beside
each start), one or two mountain plateaus and two or three towns. The game
room lists the three amounts with the map seed in the recipe box over its map
preview (and the full-window map); Cartographer's generator dialog has
PLATEAUS, MOUNTAINS and TOWNS fields (0–255).

Relief placement excludes Veruna grass-covered Breaker pieces from
sandy terrain and Taros cobblestone-edged unique23/25/27 pieces from bare low
ground. They require transitions which this placement path does not assemble.
The compatible relief families in all five worlds were visually inspected;
height checks alone cannot establish texture compatibility.

Each mana spot has ruins from its terrain theme. When the usual ring position
conflicts with construction space or an approach, arcs move outward to a clear
position using their actual footprints. Lodestone yards and routes stay clear.

Trees and rocks use separate low-frequency density fields to form clusters.
Placement reserves the real TDF footprint from its northwest anchor. Home
construction space and routes remain clear. Retail prefab features and the road
and blocker cell attributes are retained; scenery sliders control additional
scatter rather than removing authored features from those sections.

Water amount is a noise intensity, not a requested coverage percentage. Its zero
endpoint is dry on Mainland/Lakes.
Islands and themed fixed-water layouts show automatic water allocation instead
of an ineffective slider.
The extra-mana slider requests **0–6 additional spots per player**; its label
shows the requested count (`+3/P` at the default). It never removes the three
guaranteed home deposits. Small maps and constrained layouts can fit fewer complete
rounds than requested, so the actual count can stop increasing once space is
exhausted. Raising the slider retains the earlier rounds; it does not place
partial rounds that favor some players. Cartographer's Mana density field uses
the same 0–255-to-0–6 mapping.

### Scattered mana

**Scattered Mana** places neutral sacred stones, which belong to no player's
rounds, as an area density rather than a count. **Max** is the density of a
home economy holding six spots: the generator reserves an 87×87-cell square
(7,569 cells) around each home, and six spots there is one per ~1,262 cells,
a 36-cell grid pitch. **High**, **Medium** and **Minimum** halve the density in
turn (pitches 50, 71 and 100; ½, ¼ and ⅛ of Max). The map is divided into a
grid at that pitch and each grid cell gets at most one candidate, drawn from
its own seeded stream (so the scenery that follows is unchanged), tried up to
eight times. A site must be reachable flat ground with a clear 14×14 lodestone
yard, outside every reserved home economy, approach and route, at least 60
cells from every start and 24 cells from any other deposit. On a flat map Max
measures about 0.85 of the home density, since homes and their approaches take
their share; water, cliffs and hills reduce the count further. Spots take the
shipped sacred-stone tier mix (223:446:537 weak:medium:strong) and the same
ruins as other sites; a spot whose ruins find no room is omitted.

Each site's approach is reserved along its nearest home's walking-distance
field until it joins an approach already known to lead home, so scenery cannot
wall it in; the final check requires every scattered spot to be reachable from
some start. The generator's layouts are fair rather than mirror-symmetric, and
scattered spots follow the same rule: they are spread evenly but are not
mirrored, and they never enter a home economy.

Generated terrain ignores terrain replacements bundled with unrelated downloaded
maps. Explicit user overrides keep their precedence, and authored maps keep their
own texture lookup. CPU composition, full-detail terrain textures,
and distant terrain mipmaps distinguish the two lookup scopes in their caches.
The runtime provenance flag is not serialized into a bare exported TNT; a saved
map is then treated as an authored map.

## Reproducibility and verification

All generation decisions use integer arithmetic and fixed traversal orders.
Prefab discovery is sorted. The asset-free synthetic golden is
`6bf977a42891c638` (the synthetic install has no kit
sections, so the default kit amounts are a no-op there). The multiplayer gameplay-data hash includes the
coast and relief sections used by the generators, as well as the
feature-definition checks. It therefore detects differences in the source
height/feature planes, not just differences in unit stats.

### Seam hunt

`mapgen_seams` looks for terrain tiles that should not sit next to each other,
with two independent signals. (a) A retail adjacency whitelist: at every boundary
between two different terrain JPGs in the 129 shipped multiplayer and campaign maps
(and wherever a JPG meets a copy of itself out of step), the pair of keys and the
direction are recorded; a generated boundary whose pair never occurs in retail is
flagged. (b) Pixels: across each tile boundary the colour step is compared with the
step just inside each tile, and the tone of the 4-pixel strips either side with the
strip-to-strip change inside each tile, so busy textures do not false-alarm. A
candidate is flagged by (a) and strong in (b) (edge or tone score above 3); strong
(b) on a whitelisted pair is listed separately.

On retail's own 790,192 boundary segments the edge score has median 0.79 and
99.9th percentile 1.53, the tone score 0.38 and 2.90; 0.11% are strong, and with a
leave-one-map-out whitelist 0.050% are candidates (largely genuine retail seams,
such as a mission map's mid-map join). A 6,150-map sweep (every world and layout,
16×16 to 64×64, 2/4/8 players, three seeds, default, maximum and three mixed slider
extremes) finds no candidate and no strong seam. The seams this hunt catches are
hillside ramps laid beside the wrong cliffs (Aramon's dirt slopes read as rectangular
patches, Taros's as a faint lighter band, tone 1.6 to 2.6), which is why the ramp
rules above lay hillsides only as retail does.
`mapgen_seams <data> pairs` scores every hillside/cliff and hillside/hillside join
from the section art; the `mapgen_seams` test runs a smaller sweep and fails on any
candidate or strong seam.

```sh
./build/mapgen_seams /path/to/tak_data retail
./build/mapgen_seams /path/to/tak_data sweep 0 1          # or shard N of M
./build/mapgen_seams /path/to/tak_data pairs
```

Run the focused tests with:

```sh
./build/mapgen_test
./build/mapgen_test /path/to/tak_data
./build/mapgen_test /path/to/tak_data --sweep
./build/mapgen_test /path/to/tak_data --naval
./build/mapgen_test /path/to/tak_data --large
./build/mapgen_test /path/to/tak_data --mana
./build/mapgen_test /path/to/tak_data --themes
./build/mapgen_test /path/to/tak_data --ports
./build/mapgen_test /path/to/tak_data --kits
```

The asset-free test has a fixed generated-map hash and deliberately non-square
feature footprints. The data sweep covers 4,725 combinations: five worlds, three
layouts, every player count from two through eight, five seeds, three input
sizes, and zero/medium/maximum densities. It checks generator invariants and
periodically regenerates encoded recipes to compare their complete output.
The large-map check covers all five worlds and three layouts at 64×64 with eight
players, checking connectivity, mana-site ruins, and recipe reproducibility.
The naval test uses the real simulation and produces two of each ship on the
Sea Fort's build menu on a generated Islands map of every world, in both
balance modes. `--mana` checks the extra-mana slider's complete rounds.
CTest includes the asset-free test; a configured `TAK_TEST_DATA` additionally
includes the smaller retail-map roster, extra-mana, naval production, themed
layout, Ports and kit checks (`mapgen_roster`, `mapgen_mana`, `mapgen_naval`,
`mapgen_themes`, `mapgen_ports`, `mapgen_kits`, `mapgen_seams`). `--kits` generates every world on
Mainland, Lakes, Islands and its themed layout with 2, 5 and 8 players at 32×32, at
the default and maximum kit amounts: it checks determinism, that no kit had to be
reduced, an independent flood that cliffs never cut a start off, that every section
whose tiles are one whole kit piece carries that piece's heights exactly, and that
each world builds exactly the kits the survey above says it has.

The focused `mapgen_themes` test exercises all five themes, 2–8 players and
three seeds (including maximum scenery density), checking deterministic output
and terrain/resource reachability. `mapgen_ports` produces two of every Sea Fort
ship in both balance modes on the Ports layout. Windows and macOS CI run the
asset-free generator golden alongside the simulation/render regressions.

## Saved maps and multiplayer

The game room distributes generated-map recipes, not independently chosen random seeds.
Once the match starts, the host, every client (including spectators), and the server
save a reusable `Maps/Generated-<recipe SHA-256>.kmp` in their per-user folder (the
client's beside `settings.ini`, the server's `--map-cache-dir` or, without one,
`TAKengine/takserver` in its per-user folder), never in the game data folder. The
archive contains the terrain, starting positions, original recipe, and retail tile artwork.
Repeated starts of the same recipe reuse that file. Previewing/canceling does not save
it. Saved maps appear in the map picker in subsequent games.

Custom maps use [verified automatic transfer](map-transfer.md). Received maps also
remain selectable, with a short fingerprint in the name to distinguish versions.

Optional map names travel in the recipe,
are limited to 24 ASCII letters/digits/spaces/hyphens/underscores, and are used
for the saved map title and filename. The filename retains a recipe hash to avoid
collisions. Named maps also include a 16-digit recipe hash in their internal
map name and browser entry, so duplicate titles stay independently selectable.
Slider previews regenerate on release, not during dragging.
