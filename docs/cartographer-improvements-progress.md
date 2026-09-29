# Cartographer improvements — active goal

The scope is all six areas in the [usability review](cartographer-usability-review-2026-09-29.md).
This is an implementation ledger, not a claim that the complete goal is finished.
Simulation and pathfinding rules are unchanged.

## First implementation checkpoint

Implemented:

- Normalize terrain-world names; Ulasem Arena now exposes 1,261 Zhon sections.
- Share serialization between loose export, KMP saves and recovery. Always write
  CRT/restriction state, including rule-only scenarios and deletion of old content.
- Stage writes, retain previous versions as `.bak`, atomically replace a KMP,
  and roll back an ordinary multi-file export failure. Loose exports are not
  power-loss-atomic; KMP is the normal format.
- Ctrl+S saves KMP; Save As names a map and destination; loose export is explicit.
  Save errors are visible, Save As confirms replacing another map, and unsaved
  documents have a title marker.
- Compact, bounded undo/redo history covering terrain, objects, starts, metadata,
  restrictions and rules. Mouse strokes commit together; branching clears redo.
- Periodic background recovery-file writes and a startup recovery prompt.
  Interactive recovery timing/prompt still needs dedicated end-to-end coverage.
- New maps start unsaved. Resize rejects invalid dimensions and cropping through
  units, starts or regions. Stop rendering workers before terrain replacement.
- Read the client's remembered retail installation and offer a folder picker.
- Fit the window/UI scale to the display; visible File/Edit/View/Scenario/Help
  menus, map-name/file-path Open, Save As, and Fit Map.
- Text caret movement, selection, clipboard, select-all, UTF-8-safe deletion.
  Font coverage and multiline visual editing are still unfinished.
- Cull feature iteration to the visible map area and avoid restamping the same
  snapped location repeatedly during a stroke.

Validation so far:

- Document tests cover rule-only saves, metadata, backups, clearing deleted
  scenario data, failed writes, path checks, variable-length undo/redo, branching,
  saved revisions and the retained-history memory budget.
- Real SDL input tests cover properties, undo/redo, saving, reopening a KMP,
  Save As, cancellation, failed Open and UTF-8 deletion.
- Both tests passed in Release and under AddressSanitizer/LeakSanitizer.
- The original rule-only fixture now exports its full 712-byte CRT and round-trips.

## Remaining work

1. Finish recovery/overwrite/resize interaction coverage and inspect concurrent
   editing/recovery lifecycle edge cases.
2. Searchable Open/recent maps, remembered window/layout/scale, better font and
   multiline fields, complete cross-platform packaging/launch usability.
3. Search/category/faction/role browsers, placement previews, separate selection/
   erase/pan modes, multi-selection/clipboard/inspector, minimap and view controls.
4. Engine-backed terrain/buildability overlays, clickable map validation,
   reachable starts/mana/naval-output checks, full reproducible generator controls.
5. Region authoring, named units, typed rule inputs, rule reuse/reordering,
   temporary-map playtesting through the normal client/server launch path.
6. Local terrain invalidation, asynchronous expensive operations, large-map
   profiling, broader document/UI coverage and final documentation cleanup.

No retail GUI launch is needed for the completed work. The separate untracked
retail-weapon probe is unrelated and is not part of this goal.
