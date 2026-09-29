# Cartographer usability review — 2026-09-29

Reviewed the current editor source, shared map/render/save paths, packaging, and
existing documentation. Opened Ulasem Arena in the built editor, captured its
interface, and exercised exports using disposable files under
`/tmp/tak-cartographer-review`. No retail GUI was launched and no engine/editor
implementation was changed. Windows/macOS interaction was not tested live.

The reusable foundation is good: retail terrain sections, the shared terrain
renderer, procedural generation, typed CRT scenario records, and KMP packing.
The editor around those components still behaves like a development tool.
The documentation's “feature-complete” and “1:1” claims overstate the current UI.

## Confirmed problems

1. **A normal retail map can open with no terrain brushes.** Ulasem Arena's OTA
   says `kingdom=Zhon`. `SectionLibrary::scan` lowercases candidate paths but not
   the world argument used to form its prefix. The live editor reported **zero
   sections** and displayed an empty terrain palette. Starting a new lowercase
   `zhon` map found **1,261 sections**. Normalize world identifiers at the boundary
   and explain missing content in the UI. References: `sections.cpp:14`,
   `main.cpp:372`.
2. **Loose Save omits rule-only scenarios.** A synthetic CRT with one condition
   and one action, no units and no regions, round-tripped through `tnttool crt`
   correctly (712 bytes). Cartographer's loose export wrote TNT and OTA but no
   CRT. KMP export preserved the 712-byte CRT. The write condition tests units
   and regions, not rules or custom types (`main.cpp:333`). Existing CRT files
   may instead remain stale. The save representations need one shared policy.
3. **Default desktop launch is incomplete.** With no `--data`, the editor checks
   only its current directory for a retail installation and otherwise prints
   usage and exits. The Linux desktop entry supplies no data argument and hides
   the terminal. It does not reuse the client's remembered installation or offer
   a folder picker (`main.cpp:114`, `res/linux/tak-cartographer.desktop`).
4. **The interface assumes a huge display.** Scale is hard-coded to four and the
   requested window is 5,120 × 3,200. The offscreen capture confirmed that size.
   A 780 × 520 logical scripting dialog requires 3,120 × 2,080 physical pixels
   at that scale. Fixed layouts have no fit-to-window fallback (`main.cpp:56`).
5. **The command-line random export ignores Random.** `--new 8x8 --world zhon
   --random --save ...` produced exactly the same TNT hash as the flat command:
   `6cee68b017212fde7a8736432cf60246e79588d8a72a4e17af37aca58a654685`.
   The early export branch bypasses `buildFreshMap` (`main.cpp:183`).
6. **Ordinary loose output is not directly discoverable in Maps.** In the
   isolated install, plain `Maps/review.tnt/.ota/.crt` was not found. A KMP with
   `kmap/review.*` was found. The retail mount filters gameplay paths outside
   `kmap/`; the editor comment claiming ordinary loose Maps output is immediately
   playable is misleading. Make a playable KMP the normal save/publish path,
   with loose files explicitly presented as an advanced export.

## Recommended work, in order

### 1. Make editing recoverable and saving dependable

- Fix the confirmed save omissions and inconsistent export paths first.
- Add undo/redo for terrain strokes, objects, starts, metadata, restrictions,
  and scenario rules. A drag should be one history operation, not one per event.
  Use affected-cell/object deltas rather than a whole-map copy per brush step.
- Add periodic recovery saves and a recover/discard prompt after interruption.
- Use temporary files plus replacement for a single KMP, retain a previous
  version, and report write/close failures. Loose multi-file export needs staged
  writes and a coherent failure strategy; sequential truncating writes are not
  a transaction.
- Show an unsaved marker, current destination, and persistent save errors.
  Ctrl+S currently reports ordinary save failures only in the terminal.
- Treat a newly generated, never-saved map as unsaved. `applyNewMap` currently
  clears `dirty`, allowing an immediate close without a save prompt.
- Before shrinking, preview and handle starts, units and regions outside the
  new bounds. Current resize changes terrain arrays without relocating those
  objects and has no practical upper bound on typed dimensions.

Acceptance: paint/place/edit rules, undo/redo, save, reopen, and retain exactly
those edits; failed saves preserve the previous usable map; recover an unsaved
session; verify rule-only and empty-after-deletion cases.

### 2. Give it a normal desktop workflow

- Reuse the client's installation preference, with a browse fallback.
- New / Open / Recent Maps / Save / Save As / Export, with ordinary shortcuts
  and visible File/Edit/View/Scenario menus. The current top strip has only
  Terrain, Features, Units, Starts; important commands are hidden behind keys.
- Adaptive DPI and user-selectable scale; fit the initial window to the monitor.
  Remember layout/window size. Use a readable font with Unicode support.
- Proper text editing: caret movement, selection, select-all, copy/paste,
  Home/End, UTF-8-safe deletion, multiline description, inline validation.
  Current fields append characters and remove the final byte with Backspace.
- A short first-run guide and tooltips showing the action and shortcut.
- Consistent distribution: Linux packages install Cartographer; current Windows
  and macOS release bundle scripts do not explicitly package the editor.

### 3. Make terrain and object editing predictable

- Search and category filters for sections/features; faction and role filters
  plus friendly names/icons for units. Preserve internal identifiers in details.
  Thousands of unlabeled section thumbnails are not a practical browsing tool.
- Hover information and a translucent brush preview showing the snapped area.
  Display which layers a stamp replaces: its art, heights, and features travel
  together today. Offer explicit layer protection where technically sound.
- Separate Select, Place, Erase and Pan tools. Currently right-click can delete
  an object at the start of a pan; in Features mode right-drag erases instead.
- Box selection, duplicate, copy/paste, group move, bulk owner/stat edits,
  footprint outlines, facing arrows, and actual model previews where useful.
- Minimap navigation, fit map, frame selection, bookmarks, and layer visibility.
- Symmetry and brush variants should preserve authored terrain semantics.
  Do not blindly rotate terrain artwork: cliffs contain baked lighting and
  relief. Prefer compatible authored variants and validate their height edges.

### 4. Explain whether a map will play well

- Show walkability, slopes/ramps, water depth, buildability and blocked cells
  using the engine's actual terrain and movement rules.
- Validate starts (count, numbering, overlap, reachable land), mana accessibility,
  lodestone footprints, isolated land/water components, naval construction/output
  clearance, missing resources and objects outside the map.
- Report warnings in a clickable list that centers the camera on each problem.
  Current Check Map only checks the Use Only restriction against placed units.
- Keep intentional scenario restrictions as warnings where appropriate. An
  island scenario should not be rejected merely for lacking land connectivity.
- Integrate the existing random-map settings: player count, seed, layout,
  terrain/resource options, preview, regenerate and reproducible re-generation.
  The editor currently fixes generation to four players and chooses an invisible
  clock-derived seed. Expose the sanitized dimensions actually generated.

This is primarily visualization and validation around existing engine rules,
not a reason to change the hard-won pathfinding implementation.

### 5. Finish scenario authoring and add a fast playtest loop

- Named region drawing/editing and an object/region list. CRT regions are
  preserved, but the current editor has no region-authoring tool.
- Unique unit names and position controls in the inspector, including a clear
  policy for the ninth/neutral player slot represented by the file format.
- Typed rule fields: pick a unit, player, region or existing flag rather than
  spelling each raw value; validate references and show full rule text.
- Rule names, duplicate/copy/paste, reorder actions, copy player rules, and
  searchable templates for common objectives. Existing rules are numbered;
  the UI adds/removes conditions/actions but lacks these authoring conveniences.
- Maintain scenario metadata consistently when creating/removing content.
- A **Test Map** button that launches the normal client/local-server flow on a
  temporary saved snapshot, then returns to the unchanged editing session.
  Add optional trigger execution logs and named-object/region overlays.

### 6. Keep large maps responsive and make improvements testable

Source-level candidates, not claims from an editor performance profile:

- The feature rendering loop walks every terrain cell before checking whether
  a feature is visible. A 64×64 map means 4,194,304 cells per frame. Iterate the
  visible cell rectangle or occupied spatial buckets instead.
- `tilesEdited()` clears terrain chunks and scans all section keys. Repeated
  stamps while dragging should invalidate only affected areas, merge updates,
  and avoid repainting the same stamp origin repeatedly.
- Avoid synchronous thumbnail decoding and full minimap regeneration blocking
  input. Use immutable snapshots for background generation/export, progress,
  and cancellation rather than reading mutable editor state from a worker.
- Audit mutation synchronization: New/Resize directly mutate the MapView map,
  while background rendering workers can read it. The renderer has `quiesce()`;
  the editor mutation paths do not currently call it. This is a source-level
  risk requiring a targeted sanitizer reproduction, not a reproduced crash here.
- Split the 1,600-line main into document, editing commands, save/load,
  validation, and UI concerns. Share document serialization between every save
  route so the current CRT omission cannot recur in just one branch.
- Add focused document/UI workflow coverage. Shared format tests are useful,
  but there is no dedicated Cartographer CTest target covering these workflows.

## Suggested milestones

1. **Safe to use:** palette fix, save consistency, undo/redo, recovery, bounds
   validation, installation discovery, readable adaptive layout.
2. **Pleasant to build maps with:** searchable browsers, placement previews,
   selection/inspector, minimap and gameplay overlays, generator controls.
3. **Useful for mission authors:** region tools, typed rules, reusable objective
   templates, one-click playtesting and diagnostics.

Measure success with a complete task: launch from the application menu, create
an eight-player map, adjust a ramp and mana site, validate it, save/reopen it,
and start a skirmish without a terminal or manual file manipulation.
