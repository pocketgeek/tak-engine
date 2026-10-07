# TAK Engine 0.7.27

0.7.27 follows 0.7.26. It makes very large battles cheaper to simulate, adds
two Graphics options (Tactical Dots and Renderer), and fixes the build identity
that release packages report.

- **Faster large battles.** Four changes cut simulation cost in battles with
  10,000–15,000 units:
  - **Legion bookkeeping.** Legion keeps running totals of its live fields
    instead of walking every group whenever one starts, indexes each formation
    point's members instead of scanning all members, and recounts soft-obstacle
    windows only for cells that changed. These change no behavior.
  - **Held units rest.** A Legion unit that has been held in place for 60
    updates, with no detour or route committed, now gets its full update every
    other tick. It wakes at once when its orders change, it is pushed or hurt,
    the map changes, or a unit arrives in or leaves a cell touching its
    footprint.
  - **Clustered static-map repair.** When buildings, features or corpses change
    in far-apart places, Legion repairs each cluster on its own instead of one
    box spanning all of them, and finds the map's structures once per update
    instead of once per changed area.
  - **Retail's path-pending flag.** Retail answers "is a route search pending
    for this unit?" from a per-unit flag instead of a hash lookup. This changes
    no behavior.

  The timings below come from replays of the 0.7.25 Ulasem Arena stress games
  (about 15,000 units at the start) on an optimized build, with the old and
  new code run side by side on pinned cores. The host was shared, so they are
  **exploratory**, not benchmarks:
  - **Legion, order-heavy stress game:** the mean tick fell from about 30.5 to
    21.3 ms, and ticks over the 33.3 ms real-time budget from 8,579 to 398 of
    18,001. Before, the game ran at about 1.1× real time; now at about 1.6×.
  - **Legion, resting and repair:** on top of that, these two changes cut the
    mean tick a further 6–9% in two of the three Legion stress games, in two runs with
    the cores swapped. The largest Legion map-repair cost in a single tick fell
    from about 38 to 10 ms. On the all-AI stress game the gain was within the
    measurement noise.
  - **Retail:** movement time fell by about 16% and the mean tick by about 7%.
  - Large battles are still demanding: in the opening, with 10,000–15,000
    units, Legion is still close to the real-time budget on a fast desktop CPU.

  Retail behavior is unchanged: its navigation checks and recorded stress
  games replay identically. Legion games play out slightly differently, so the
  protocol is now 238. In testing, held units still moved as smoothly as
  before, and the formation tests pass.
- **Tactical Dots.** A new **Options → Graphics → Tactical Dots** toggle (off by
  default; not in the original game). When you are zoomed far out, every unit
  is drawn as a square in its player's colour, like the minimap, instead of
  its model. Enemies appear only where the minimap would show them. Buildings
  are bigger squares, flyers sit at their flying height, selected units get a
  white outline, and click and box selection work on the dots. The new
  **Tactical Dots Zoom** slider (0–100%, default **20%**) sets how far out
  dots take over. It is measured along your zoom-out range, not as a fixed
  zoom: 0% means only fully zoomed out, 100% means from normal size outwards.
  Dots stay on until you zoom in a little more than one wheel notch past the
  setting, so the view does not flicker. The slider is greyed out while Tactical
  Dots is off. Dots are much cheaper to draw: on a
  16,000-unit test at 1920×1080, drawing took about 2 ms per frame instead of
  35 ms (an exploratory measurement). See
  [the user guide](user-guide.md#graphics).
- **Renderer option.** **Options → Graphics → Renderer** picks the graphics
  backend: **Auto** (the default and the previous behavior) or any backend that
  SDL offers on your machine, such as OpenGL, OpenGL ES 2, Direct3D 11, Metal
  or Software. It applies after a restart, and the row says **RESTART
  REQUIRED** until then. If the chosen backend is missing or fails to start,
  the game falls back to Auto for that session and shows a notice on the main
  menu. Your choice stays saved and is retried at the next launch, and the row
  says **UNAVAILABLE**. A backend chosen by name also keeps SDL's draw-call
  batching on; without that fix it drew about 20% slower than the same backend
  under Auto. The backend in use is shown in the Benchmark results and as a
  new **RENDERER** row in the stats panel. Interface text is now aligned to
  whole pixels, which fixes faint, dotted text under the Software renderer. See
  [Renderer](user-guide.md#renderer) and
  [the black-screen troubleshooting](user-guide.md#the-game-will-not-start-or-shows-a-black-screen).
  Only Linux was tested by hand; Direct3D and Metal were not.
- **Build identity fixed.** Linux packages and Windows builds now report their
  real build id. 0.7.26 Linux packages said `build unknown`, and Windows builds
  added a spurious `-dirty`; macOS was already correct. CI confirmed the fix on
  the version commit: every Linux package and both Windows builds report a clean
  id.

**Compatibility:** protocol **238**, replay format **11**, generator version
**8**, campaign payload **4** and Crusades SQL schema **9** (all but the
protocol unchanged since 0.7.26, checked in source against `v0.7.26`). Update
clients and servers together; 0.7.26 clients cannot join. Replay playback
requires the exact simulation protocol, so 0.7.26 recordings (protocol 237)
need 0.7.26. Generated-map recipes and saved `.kmp` maps from 0.7.26 remain
usable. Saved settings carry over: the new options start at their defaults
(Renderer Auto, Tactical Dots off, Tactical Dots Zoom 20%).

No new dynamic runtime dependencies or retail assets are included. See the
[release validation report](release-0.7.27-validation.md).
