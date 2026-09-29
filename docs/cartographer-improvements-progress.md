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

## Browser and selection checkpoint

Implemented searchable terrain/feature palettes and unit faction/role filters,
with friendly unit names, internal identifiers and hover details. Place, Select,
Erase and Pan are separate controls; right-drag always pans. Unit selection
supports boxes, group dragging, copy/cut/paste, duplicate, deletion and bulk
property edits. Copies retain statistics and ownership but clear unique names
so they cannot accidentally alias scenario trigger references. Group operations
preserve relative spacing and reject groups that cannot fit within the map.

The canvas now shows translucent snapped placement previews, selected-unit
footprints/facing, and a clickable minimap with the current viewport and starts.
The minimap refreshes after save/replacement; continuous terrain-edit refresh is
still pending. Starts have a player list with click-to-center navigation.

Release editor/document tests pass, including the new selection boundary and
property-preservation checks. Captured and inspected Ulasem Arena with the new
browser and minimap. This checkpoint has not yet had Windows/macOS interactive
verification or full browser mouse-interaction coverage.

## Initial engine validation checkpoint

Check Map now uses a private engine World with the real movement classes,
feature obstacles and placement checks. It diagnoses unknown/restricted unit
types, out-of-bounds units, invalid owners, duplicate unique names, unsuitable
monarch starts, duplicate starts, invalid region bounds/names, unknown opcodes,
unresolved region references and operands too long for CRT serialization.
Scenario > Next issue centers the canvas on each located result. Authored unit
placements rejected by normal construction rules are warnings, not save blockers.
Inclusive and reversed region bounds and empty whole-map references follow the
scenario runtime's actual semantics.

This is not yet complete map analysis: unit-to-unit overlaps, reachable mana,
naval output clearance, terrain overlays and asynchronous analysis remain.
Validation currently uses the editor's standard balance registry. Results are a
snapshot; rerun after edits. Three Release tests pass, including synthetic map
validation against actual engine placement, and document/UI workflows.

## Region authoring checkpoint

Scenario > Regions opens a scrollable list with create, edit, delete and locate
operations. Properties edit the name and inclusive corner cells, allowing a
region to be moved/resized precisely; canvas overlays show its extent and name.
View > Toggle regions controls visibility. Region names are unique, reserved
whole-map tokens are rejected, renaming rewrites typed rule location references,
and deletion refuses regions still referenced by rules. These operations use the
existing document history and CRT save path. Canvas drag handles are still pending.

Rule parameter dialogs now offer scrollable location, unit-type and player
choices while retaining existing authored values. Numeric operands filter input,
and all operands must fit the CRT limit before the rule is changed. This is not
complete semantic rule validation or the planned template/reordering workflow.

Three tests pass in Release and AddressSanitizer/LeakSanitizer. Coverage includes
region rename/reference preservation, failed-edit atomicity, referenced deletion,
CRT roundtrip, and actual SDL menu/create/save interactions with saved bounds
verified from the resulting bundle.

## Remaining work

1. Finish recovery/overwrite/resize interaction coverage and inspect concurrent
   editing/recovery lifecycle edge cases.
2. Searchable Open/recent maps, remembered window/layout/scale, better font and
   multiline fields, complete cross-platform packaging/launch usability.
3. Browser/selection interaction coverage, layer protection, feature selection,
   model previews where useful, additional view controls and live minimap refresh.
4. Engine-backed terrain/buildability overlays, clickable map validation,
   reachable starts/mana/naval-output checks, full reproducible generator controls.
5. Region canvas manipulation, expanded typed-rule validation, rule reuse/reordering,
   temporary-map playtesting through the normal client/server launch path.
6. Local terrain invalidation, asynchronous expensive operations, large-map
   profiling, broader document/UI coverage and final documentation cleanup.

No retail GUI launch is needed for the completed work. The separate untracked
retail-weapon probe is unrelated and is not part of this goal.
