# TAK Engine 0.7.25

0.7.25 follows 0.7.23, the last published release. 0.7.24 was prepared but
never published; everything from it that still ships is listed here.

- **Two pathfinding choices: Retail (the default) and Legion.** Game creation
  switches between them; the lobby shows the host's choice, and campaign
  missions and Crusades battles always use Retail. The experimental Retail+,
  Flowfield and Cooperative modes prepared for 0.7.24 were removed before
  release; a saved preference for one of them falls back to Retail. Their
  designs and measurements remain in git history.
- **Legion** (experimental) plans one integer route field per army and
  footprint on the same footprint-legality rule the mover enforces, so groups
  route round walls together, slide along jagged terrain, and stop when they
  are genuinely trapped instead of spinning or rocking. A group right-click
  shares one destination with formation slots; formations fan out
  (“pinwheel”) round wall ends instead of folding into single file. Groups keep
  to lanes when meeting opposing traffic, plan round standing blocks of idle
  units, and idle units of the same player step aside to open a lane for a
  held unit. It routes moves, fight-moves, patrols, attack, chase and guard
  approaches, and build, repair, reclaim, transport, factory-exit and parking
  approaches, for ground units, boats and hovercraft. Flyers keep retail
  flight. Known limits: dense crowds of units with distinct packed goals can
  leave a unit sealed out; with about 2,000 moving units Legion is the
  cheaper mode on shared destinations, but Retail costs 10–48% less per tick
  in other crowd scenes; idle units of other players never step aside; and
  Legion does not model unexplored terrain separately.
  See [Legion](legion-pathfinding.md#known-weaknesses).
- **Legion landing and obstacles.** In Legion games a flyer never touches down
  on another landed or descending flyer or on a mobile ground unit; it picks
  another site with retail's own search. Landed flyers count as standing
  obstacles that ground groups route round. Retail keeps retail's behavior,
  in which flyers can land on top of each other.
- **Retail pathfinding audit.** More of the original game's movement was
  checked against the retail binary and ported, changing Retail behavior where
  the earlier port differed:
  - Flyers use retail's landing-site check and its persistent airborne
    occupancy grid in both modes. The engine-only “no shared landing spot”
    rule was removed, because retail has none.
  - Numbered groups and formations in Retail games use retail's group
    pacing (straggler slowdown, the group speed cap and re-forming) instead
    of an invented rule. In Legion, a formation member rejoins only once the whole
    formation is at rest, so settled formations no longer keep reshuffling.
  - Every skirmish and multiplayer player gets retail's route-search budget
    class; units carried by a transport explore from their carrier; a move to
    the unit's own cell is submitted as retail does.
- **Shift queues every order.** Shift-patrol after a move now closes the loop
  back to where the patrol starts, as in retail. An armed order (move, fight,
  attack, patrol, guard, heal, load, unload, reclaim) stays armed while Shift
  is held, and the Shift order preview marks fight-move legs. Queued Legion
  group moves share the clicked point.
- **Builder controls.** Releasing Shift after placing queued sites clears the
  selected build icon without clearing the builder or its orders. The build
  menu appears only for one selected builder or production building and uses
  the normal UI cursor. Holding Shift previews remaining area-build and
  area-clear stops together with later queued orders.
- **Extra Mana Spots** on generated maps requests 0–6 additional deposits per
  player, independent of map area, placed in balanced rounds near the home
  areas; available terrain can reduce the count, which the preview reports.
- **One graphics filter, as in retail.** The Terrain AA and Model AA
  supersampling options are gone. A single **Bilinear Filtering** toggle,
  retail's `BiLinearFilter`, defaults **off** and smooths only unit model
  textures and unit shadows; terrain, scenery and the interface stay
  point-sampled, as in retail. **Smooth GUI Art** and **Smooth Movies** return
  as options (default off); Smooth GUI Art applies immediately. **Hardware
  Cursor** and **Smooth Motion** move to the Graphics section, beside Shadows
  and Trees Sway in Wind.
- **Smoother large games.** The first retail route search for a unit shape no
  longer stalls a tick: building its search plane went from 37–49 ms to about
  3.5 ms on a 768×768 map and from 16.5 ms to 1.9 ms on Ulasem Arena, with
  identical results. Exploration updates skip already-explored blocks, and
  Retail's search reuses scratch storage, without changing behavior. A
  parallel terrain-height sampling race is fixed.
- **`takserver --status [--port N]`** prints JSON counts of running games,
  lobbies and connected clients for local monitoring, then exits. It also works
  with TLS/ACME servers. See [server status](public-server.md#querying-server-status).

**Compatibility:** protocol **236**, replay format **11**, generator version
**8**, campaign payload **4** and Crusades SQL schema **9** (the last two
unchanged since 0.7.23). Update clients and servers together; 0.7.23 clients
cannot join. Replay playback requires the exact simulation protocol, so
0.7.23 recordings need 0.7.23. Generated-map recipes from 0.7.23 (generator 7)
cannot be regenerated; saved `.kmp` maps remain playable. Saved Retail+,
Flowfield or Cooperative choices load as Retail; old AA settings are dropped.

No new dynamic runtime dependencies or retail assets are included. The README
and Cartographer gallery is refreshed with renderer captures from this
version. See the [release validation report](release-0.7.25-validation.md).
