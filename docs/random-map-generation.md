# Random map generation

New recipes use generator version 4. The map ID still carries the seed and
parameters in `~gen1~` followed by hexadecimal bytes; the first two payload bytes
are the generator version. Versions 1–3 keep their original generation algorithms and size limits.
Protocol 193 introduced version-3 generation and verified map-transfer support;
The current checkout uses protocol 203 for version-4 recipes. Existing pathfinding rules are unchanged.

## Layout and placement

- **Mainland:** noise-shaped coasts with connected starting areas and broad,
  reserved ground routes toward the interior.
- **Lakes:** inland water with dry map edges and connected starting areas.
- **Islands:** separated home islands, connected sea lanes, and open water for
  naval construction and ship departure. Travel between islands needs boats or
  aircraft. Water allocation is automatic.

The menu offers square sizes 8, 12, 16, 20, 24, 32, 40, 48, 56, and 64. It skips sizes that cannot
support the selected layout/player count:

| Layout | 2–4 players | 5–8 players |
| --- | --- | --- |
| Mainland | 8×8 minimum | 12×12 minimum |
| Lakes | 12×12 minimum | 16×16 minimum |
| Islands | 16×16 minimum | 24×24 minimum |

The API also accepts rectangular dimensions in 32-cell section multiples, with
per-axis space requirements. One map-size unit is 32 simulation cells.

Starts are planned before terrain is painted, with a minimum 72-cell separation,
level home areas, and clear 24×24-cell construction squares. Home economies are
reserved before hills, scenery, or coast sections can occupy them. Each start
receives the same three sacred-stone strengths at the same offsets. Additional
mana sites are admitted in complete rounds: equal counts and strengths, with
walking-distance bands of ±12 cells around the round's target. A candidate must
be closer by walking distance to its owner than to another reachable start.
If any player lacks space for another round, the round is omitted for everyone.
This is an economic fairness rule, not a promise of perfectly symmetric terrain.

The generator uses the existing retail terrain classifier and footprint
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
small islands with large reserved home areas. This does not attempt to assemble
every retail cliff, plateau, town, or mountain kit.

Each mana spot has ruins from its terrain theme. When the usual ring position
conflicts with construction space or an approach, arcs move outward to a clear
position using their actual footprints. Lodestone yards and routes stay clear.

Trees and rocks use separate low-frequency density fields to form clusters.
Placement reserves the real TDF footprint from its northwest anchor. Home
construction space and routes remain clear. Retail prefab features and the road
and blocker cell attributes are retained; scenery sliders control additional
scatter rather than removing authored features from those sections.

Water amount is a noise intensity, not a requested coverage percentage. Its zero
endpoint is dry on Mainland/Lakes; the preview reports measured coverage.
Islands show automatic water allocation instead of an ineffective slider.
The extra-mana slider does not remove the three guaranteed home deposits.

Generated terrain ignores terrain replacements bundled with unrelated downloaded
maps. Explicit user overrides retain their precedence, and authored maps retain
their existing texture lookup. CPU composition, full-detail terrain textures,
and distant terrain mipmaps distinguish the two lookup scopes in their caches.
The runtime provenance flag is not serialized into a bare exported TNT; a saved
map is subsequently treated as an authored map.

## Reproducibility and verification

All generation decisions use integer arithmetic and fixed traversal orders.
Prefab discovery is sorted. The multiplayer gameplay-data hash includes the
coast and relief sections used by the generators, as well as its existing
feature-definition checks. It therefore detects differences in the source
height/feature planes, not just differences in unit stats.

Run the focused tests with:

```sh
./build/mapgen_test
./build/mapgen_test /path/to/tak_data
./build/mapgen_test /path/to/tak_data --sweep
./build/mapgen_test /path/to/tak_data --naval
./build/mapgen_test /path/to/tak_data --large
```

The asset-free test has a fixed generated-map hash and deliberately non-square
feature footprints. The data sweep covers 4,725 combinations: five worlds, three
layouts, every player count from two through eight, five seeds, three input
sizes, and zero/medium/maximum densities. It checks generator invariants and
periodically regenerates encoded recipes to compare their complete output.
The large-map check covers all five worlds and three layouts at 64×64 with eight
players, checking connectivity, mana-site ruins, and recipe reproducibility.
The naval test uses the real simulation and produces two of each of the six Sea
Fort ship types in every world and both balance modes (60 production cases).
CTest includes the asset-free test; a configured `TAK_TEST_DATA` additionally
includes the smaller retail-map roster and naval production checks.

The development audit also compared 30 version-1/version-2 maps against the
pre-change generator, with identical heights, features, tile references, names,
and starts. Windows and macOS CI run the asset-free generator golden alongside
the existing simulation/render regressions.

Local acceptance for this change: 91 Release and 94 optimized-Debug CTests
passed; GCC and Clang matched the synthetic golden and all 105 retail-roster map
hashes. Thirty-second Crusades client/referee matches on generated Mainland and
Islands each reached tick 900 without a mismatch. The accelerated terrain-cache
regression also passed on a generated map, covering mipmaps, panning, zoom,
filtering, edits, and renderer reset.

## Saved maps and multiplayer

The lobby distributes generated-map recipes, not independently chosen random seeds.
Once the match starts, the host, every client (including spectators), and the server
save a reusable `Maps/Generated-<recipe SHA-256>.kmp` under their data root. The archive
contains the terrain, starting positions, original recipe, and retail tile artwork.
Repeated starts of the same recipe reuse that file. Previewing/canceling does not save
it. Saved maps appear in the map picker in subsequent games.

Custom maps use [verified automatic transfer](map-transfer.md). Received maps also
remain selectable, with a short fingerprint in the name to distinguish versions.

### Version 4 validation (2026-09-30)

- All 4,725 terrain/layout/player/seed/size/density cases passed, including a
  surrounding-ruin check for every mana deposit and footprint-aware route checks.
- All 15 combinations of five worlds and three layouts passed at 64×64 with
  eight players; regenerating their recipes produced identical maps.
- GCC Release and Clang ASAN produced identical hashes for all 105 roster maps.
- The version-3 synthetic golden remains `9258a896baaf4285`; old recipes retain
  their original generator and dimension limits.
- Thirteen focused tests passed in each of Release, optimized Debug, and ASAN,
  including generated naval production, map transfer, Cartographer generation,
  AI behavior, and results-screen texture cleanup.
- A live 64×64 generated-map network test reached 300 matching ticks on host,
  peer, referee, and late spectator (`b471a5694f12d9e5`). Host, peer, and server
  each saved the generated map for later selection.
