# TAK Engine 0.8.0

0.8.0 follows 0.7.28. macOS downloads are now signed with a Developer ID and
notarized by Apple, so they open like any other app. A multi-unit selection
shows the whole group in the information bar, with one portrait per unit type.
A new **Texture Filtering** option can smooth the whole game area when you zoom
in. Factories no longer jam their own doors with a large unit, and Legion
formation flyers ride over the front of their army. Flying transports land
where they drop their passengers instead of flying far away. The Cartographer
map editor gains symmetry tools, terrain copy and paste, and a grouped terrain
browser.

- **macOS: signed and notarized.** The app inside the DMG and the ZIP is signed
  with a Developer ID certificate and the hardened runtime, notarized by Apple
  and stapled, and the DMG itself is signed, notarized and stapled too.
  Gatekeeper opens it on the first launch without the old right-click →
  **Open** step. The Cartographer app in the same downloads is signed the same
  way. The `-debug` ZIP is notarized as well; its bare command-line programs
  cannot carry a stapled ticket, so macOS checks them online the first time.
- **The information bar shows the whole selection.** With two or more units
  selected:
  - The bar shows **N SELECTED** and one portrait per unit type, each with a
    count. The first selected unit's type sits in the usual portrait spot and
    the others run left from it, most numerous first. Hovering a portrait shows
    the unit's name.
  - **Left-click** a portrait to select only that type; **Shift+left-click**
    removes that type from the selection.
  - The portraits stay full size all the way to the left edge of the screen,
    drawn over the original game's end-cap scroll art. Only when a full-size
    strip would pass that edge do they shrink, then fold into two rows, then end
    with a **+K** tile for the K types not shown.
  - The health bar is the total health of the selected units out of their
    total maximum. The mana bar totals only the units that have mana, so a few
    priests among knights show their own pool rather than a sliver of it.
  - No single unit's name is shown. If the selection is exactly one group or
    formation (all of it, or what pressing its number selects, which leaves out
    its builders), its name is shown instead: **GROUP 3**, **FORMATION 2**.
    Units that a group's builder has just started building do not count
    against the match.
  - The status text (**STANDBY**, **N SELECTED** and so on) is now centred in
    the gap between the two pairs of bars, so a long unit name such as
    **BEAST HANDLER** no longer runs into it.

  A single selection draws exactly as before. See
  [the user guide](user-guide.md#selection).
- **The squad marker is solid green.** The group or formation number under each
  unit keeps its shape, size and place, but is drawn in one flat green instead
  of the shaded, darker-edged retail glyph, so it reads clearly over terrain.
- **Texture Filtering** (Options → Graphics) replaces Bilinear Filtering with
  three levels for the game area:
  - **Off** (the default): nothing is smoothed, as in the original game.
  - **Units**: exactly the old Bilinear Filtering on, the original game's own
    filter for unit textures and unit shadows.
  - **Everything**: Units, plus terrain, trees, rocks and other scenery and
    their shadows, and effects (explosions, fire, smoke, projectiles and
    lightning), but only while you are zoomed in past 150%; it switches back
    off below 140%. Not in the original game, which had no camera zoom.

  Filtered terrain shows no seams between tiles, and effects have no dark
  fringe. Off and Units draw exactly as the old toggle did. A saved Bilinear
  Filtering choice loads as Off or Units. See
  [the user guide](user-guide.md#graphics).
- **Factories no longer jam their door (protocol 255).** A Barracks that had
  just finished a Catapult could place its next unit, a Knight, on its pad
  while the Catapult was still leaving; the two overlapped, neither could move,
  and the factory was stuck until one was killed. A factory now admits its next
  ground unit only where that unit's whole footprint is free of other units,
  moving ones included, as the original game's build routine does, and retries
  after a short random wait (7 to 21 updates, about a quarter of a second to
  0.7 seconds) when the pad is occupied. Every shipped ground factory was tested
  with its largest and smallest ground units in both orders. See
  [the factory output notes](factory-output-2026-10-12.md).
- **Flying transports land at the drop point (protocol 256).** After unloading,
  the Zhon Roc, the Taros Ghost Ship and the Creon Aerial Juggernaut flew on
  1,000 to 1,500 pixels along the course they had used while their passengers
  stepped out before landing. As in the original game, a flying transport whose
  unload leaves it with no further orders now stands by at once and lands within
  a few updates where it let its passengers go. Orders queued behind the unload
  still run. Ships and the siege tower already stopped at the drop point and are
  unchanged. Every shipped transport, in both balances and both pathfinding
  modes, was tested with one passenger and a full load.
- **Legion: formation flyers ride over the front (protocol 254).** In a Legion
  formation or Ctrl+number group that mixes ground units and flyers, the flyers
  now hold rows of stations over the front of the ground units instead of over
  their middle, follow the front round corners and when the group turns, and
  on arrival land in a line just in front of the ground units' destinations
  instead of among them. Over a single unit or a narrow column the flyers stand
  in a block no deeper than twice its width instead of a long file. Retail
  pathfinding is unchanged.
- **Legion: walking pace matches movement.** A Legion ground unit that is
  slowed (turning, or keeping pace with the units ahead) now plays its walk
  animation at the speed it actually moves instead of at full pace, so it no
  longer appears to slide or run on the spot. Display only; the simulation is
  unchanged.
- **Cartographer.**
  - **Symmetry** menu: left/right, top/bottom, either diagonal, 2-way and
    4-way rotation and a 4-way mirror. **M** turns on live symmetry, which
    mirrors placed, moved and erased units, features and start positions and
    stamps the mirror-image terrain section; **Shift+M** (Symmetrize) copies a
    half, quadrant, triangle or the selection onto the rest of the map after
    showing exact counts, as one undo step. Terrain art is never flipped:
    each section is replaced by its mirror twin, and areas with none are listed
    (**Symmetry → Locate unmirrored terrain**).
  - **Terrain copy and paste**: with the Terrain tool in Select mode, drag a
    rectangle, **Ctrl+C**, then **Ctrl+V** for a click-to-paste brush with a
    preview. It carries tiles, heights, trees, rocks and units, and survives
    opening another map.
  - **Grouped terrain browser**: sections are grouped by kind (cliffs,
    transitions, water and coast, paths and roads, and so on) and by their
    original folders, numbered variants share one thumbnail (click the
    selected piece again for the next variant), and pieces get readable names
    such as *Cliff · NE outer · variant 2*. Search still covers every group.
  - **Wider font coverage**: the editor's built-in font adds Armenian,
    Georgian, common symbols, kana, Hangul and common Han characters, laid out
    double-width; characters it lacks show a replacement box of the right
    width. Right-to-left and shaped scripts are not supported.
  - The editor's source was split from one very large function into
    per-concern files, with no change in behaviour.
- **Other fixes.**
  - **Reclaim**: the reclaim cursor and the click now pick the same wreck,
    using the wreck's own size; before, they could disagree near a wreck whose
    feature is a different size from the unit that died. The area-reclaim box
    preview now marks only what will really be cleared (reclaimable features,
    sampled as the game samples the box) instead of every rock or decoration it
    touches. The **Revive** cursor uses the same pick circle.
  - **Override packs**: a full disk or tight cache quota no longer deletes a
    good cached pack or rejects a pack that had already been verified; the
    save failure is reported and the game goes on.
  - **Failed launches from the menu** (a local server that will not start, a
    refused sign-in) return to the screen they came from: a notice over the
    title screen for single-player, the campaign picker, or the Benchmark
    menu, instead of the multiplayer sign-in dialog. A local server that dies
    at startup is detected in under a second instead of after a ten-second
    wait, and on Windows the game window no longer vanishes on such a failure.
  - Match setup builds its options, slot table, replay header and simulation
    configuration in one place on the server, the client and the replay
    loader, so a fix can no longer land in only some copies. The state hashes,
    replays and network messages are unchanged.
  - A release `takclient` no longer contains the developer test harness.
- **For contributors.** Windows, Linux and macOS CI now cache compiles with
  ccache (the Windows x64 job spent about 14 of its 25 minutes compiling from
  nothing), superseded caches are pruned so they no longer fill the shared
  quota, and the Legion scenario selection runs on macOS arm64 only.

**Compatibility:** protocol **256**, replay format **12**, generator version
**8**, campaign payload **4** and Crusades SQL schema **9** (replay format,
generator, campaign payload and schema unchanged since 0.7.28, checked in
source against `v0.7.28`). Update clients and servers together; 0.7.28 clients
cannot join. Replay playback requires the exact simulation protocol, so 0.7.28
recordings (protocol 253) need 0.7.28. Generated-map recipes and saved `.kmp`
maps from 0.7.28 remain usable. Saved settings carry over; a saved Bilinear
Filtering choice becomes Texture Filtering Off or Units.

**Known limits.** The factory fix ports the original game's site test and retry
wait, but not its release of finished units (rally points, or else parking
them in a ring round the factory): measured, that ring parks the army where the
skirmish AI looks for its next building site, so units still leave by the
existing exit step. Legion's other accepted limits from 0.7.28 stand; see
[Legion's known weaknesses](legion-pathfinding.md#known-weaknesses).

No new dynamic runtime dependencies or retail assets are included. The release
Cartographer is about 1.8 MB larger, from its wider built-in font. See the
[release validation report](release-0.8.0-validation.md).
