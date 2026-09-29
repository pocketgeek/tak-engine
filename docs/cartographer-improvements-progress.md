# Cartographer improvements — active goal

The scope is all six areas in the [usability review](cartographer-usability-review-2026-09-29.md).
This is an implementation ledger, not a claim that the complete goal is finished.
Simulation and pathfinding rules are unchanged.
Later checkpoints supersede earlier outstanding items; the final Remaining Work
list tracks what is still unfinished.

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

## Generator controls checkpoint

New Map > Random now opens settings for the full 64-bit seed, player count,
Mainland/Lakes/Islands layout, tree/rock/mana density, water intensity and relief.
Parsing rejects overflow, partial numbers and out-of-range values without
changing the accepted settings. Generated map descriptions record the engine's
exact sanitized recipe. The editor uses the existing generator unchanged.

Random generation runs in a worker while a busy modal keeps rendering/input
responsive and prevents replacement of its borrowed VFS. Escape discards the
result when the worker finishes; it does not interrupt generation internally.
The old document is retained until successful completion. Generation failures
are visible. Flat-map generation also stages the new world's section library
before replacing the document.

All three Release editor tests pass. Parameter roundtrips and reproduced engine
terrain/start output use synthetic base assets (the generator intentionally
ignores map-resource overrides). The follow-up below adds preview and interaction coverage.

## Generator preview and minimap checkpoint

Generated results remain separate from the document until accepted. The preview
shows the actual terrain overview and numbered starts, with Accept/Discard.
Escape during background generation discards the finished result without
replacing the original. A visible window-title status distinguishes generation
and preview. The SDL workflow test verifies busy cancellation, preview discard,
acceptance, dirty/saved state and the persisted seed/player recipe.

Minimap composition now averages each distinct terrain block once per pass and
reuses that color for repeated blocks. This preserves the original integer
averaging and palette mapping. A 63x63 Zhon flat-map export was byte-identical to
the pre-change optimized-debug binary's output (TNT SHA-256
`630a89fb7bc1dba7a4fe0b0d6244c2b6e014f2943acf4868d2b05a2bcd3981b9`).
A single local end-to-end sample was 0.32s current Release versus 0.53s older
optimized Debug; different build configurations and one sample mean this is not
a controlled general performance claim. Varied-terrain profiling remains.

All four Cartographer tests pass in Release and AddressSanitizer/LeakSanitizer.

## Cross-platform packaging checkpoint

Windows/macOS release and debug bundle recipes now build and ship Cartographer.
The Windows executable uses the GUI subsystem and the existing compass icon;
NSIS installs it with a Cartographer shortcut. macOS includes a separate signed
Cartographer app in the DMG/zip, with its own bundle ID and compass icon. The
Arch package now includes the editor, icon and desktop entry; Linux debug
archives include the editor too. Platform workflows run the asset-free editor
checks and include the editor in static-library import verification.

Linux editor tests pass. YAML and plist parsing, icon container checks, Windows
resource compilation and MinGW source syntax checking passed locally. Full
Windows linking could not run because the pre-existing cross-build points to a
missing `/tmp/tak-ffmpeg-win`; macOS linking/signing cannot be verified here.
CI package build/launch evidence remains required before calling packaging
fully verified. No additional runtime library dependency was introduced.

## Brush protection and view controls checkpoint

Edit > Brush: protect objects toggles whether terrain stamps replace the object
layer. Protected stamps retain existing object anchors and add no prefab objects;
road/blocker markers still follow the new terrain on cells without objects.
The TNT format has one feature/marker slot per cell, so an existing object takes
precedence where a new terrain marker would otherwise occupy that cell. Terrain
art and heights remain coupled; rotating baked-lighting artwork is not offered.
The menu checkmark and status bar show the active policy.

View now includes Frame selected units and store/restore of one session view
bookmark. Bookmarks are navigation state, not saved map content or persistent
preferences. Four Release tests pass; new brush cases cover protected objects,
marker replacement, remapping prefab names and off-map no-ops.

## Terrain overlay checkpoint

View offers movement, buildability, water-depth and slope overlays. Movement and
buildability use the selected Units-palette type and a private engine World with
its real movement classes and map features. The legend explicitly excludes
placed-unit occupancy; flying-unit movement represents air transit, not landing.
Water depth uses the minimum of the four terrain corners and slope their spread.
Overlays cover individual map cells and clear after document changes so stale
analysis is not displayed as current. Hide terrain overlay returns to normal art.

Analysis runs in a worker with a busy/cancel modal protecting its borrowed asset
registry/VFS. Rendering remains active. Five Release editor tests pass, including
per-cell buildability comparison to World::canPlace, depth/slope corner cases,
and the SDL menu/show/hide workflow without modifying the document.

## Rule reuse and ordering checkpoint

The scripting panel now copies, pastes and duplicates groups, conditions and
actions; moves the selected item up/down; and copies/appends a player's complete
rule set. Copies preserve operands and are independent of their source. Actions
cannot be pasted into condition slots. The active column is marked, and moved or
inserted selections scroll into view. Ctrl+C/V/D and Alt+Up/Down mirror the visible
controls. Player paste appends instead of replacing existing rules.

Focused tests cover operand preservation, kind rejection, independent records,
boundaries and group/action ordering. The SDL workflow creates a rule, duplicates
it with Ctrl+D, saves, and verifies both CRT groups. Rule names, searchable
objective templates and fuller semantic validation remain unfinished.

## Recovery and open-archive replacement checkpoint

A two-process integration test edits an installed map, advances the injected
recovery clock, waits for a real background recovery write, then exits abruptly
without editor cleanup. A fresh process accepts recovery, saves the restored
map and verifies the unsaved metadata plus removal of recovery copies. This
caught and fixed name-based recovery selecting the retail original: recovery
now uses the exact archive member path. Archives without terrain are rejected.
The production clock and native recovery prompt are unchanged; tests inject only
those two dependencies.

Windows archive mappings now permit delete sharing so an editor can atomically
replace an open map while existing readers retain their old mapped snapshot.
The document test holds and reads an archive across replacement; platform CI
will exercise this specifically on Windows. Editor virtual paths no longer use
Windows locale-dependent filesystem conversions, and native save/backup paths
retain UTF-8 or native path types. A Unicode directory/filename save is covered.

All Release targets were rebuilt after the shared archive change. The focused
Release editor/VFS/map-transfer suite passes (9 tests), as do all 6 editor tests
under AddressSanitizer/LeakSanitizer. Multi-instance recovery ownership,
recovery destination metadata and remaining resize/overwrite interactions still
need scrutiny; this checkpoint does not claim every recovery edge case is done.

## Open browser and preferences checkpoint

Open now combines a searchable installed-map list and the twelve most recent
maps with the existing direct-path field. Filtering is cached until the query
changes. Clicking a result opens its exact virtual or native path, preserving
the unsaved-change confirmation. Native files accept case-insensitive KMP/TNT
extensions and retain their own folder/name for subsequent saves.

Window dimensions and UI scale are stored in the editor's preference directory;
recent paths are deduplicated and bounded. Preference writes use staged
replacement/backups. View > Smaller UI / Larger UI adjusts the stored scale,
with the effective scale capped to keep dialogs within the available display.
Six Release tests pass, including preference/path roundtrips and an SDL workflow
that filters and opens a recent map. Unicode paths are preserved in preferences;
Unicode glyph rendering and multiline field display are still unfinished.

## Multiline metadata checkpoint

Descriptions now wrap and scroll, with mouse caret placement, line-aware
Up/Down/Home/End, selection and Shift+Enter for new lines. The bitmap font now
counts UTF-8 code points instead of bytes and includes previously missing ASCII
punctuation. Unsupported glyphs show a placeholder; full Unicode font coverage
remains unfinished.

The save/undo workflow test exposed that raw newlines break OTA assignments.
OTA output now keeps a readable single-line legacy value and, only when needed,
adds a `[TAKText]` section containing hexadecimal UTF-8 bytes for exact free-text
restoration. This is an engine extension, not a claimed retail feature. Readers
that ignore the extension get the sanitized single-line text. Ordinary metadata
needs no extension. The TXT sidecar still contains the full description. Shared
TDF parsing is unchanged. Tests cover newlines, whitespace, URLs, braces, Unicode,
malformed extensions and the real editor's undo/redo/save/reopen sequence.

## Local terrain invalidation checkpoint

Painting now invalidates only intersecting cached terrain images, including
neighbours whose filtering gutters sample the edited blocks. It preserves source
mips and other resident chunks, queues section decodes from just the stamp area,
and no longer stops the workers before every stamp. Chunk jobs own immutable tile
snapshots; an edit cancels pending results by generation so an older image cannot
overwrite the new terrain. Pending jobs are still canceled together, so rapid
painting can defer otherwise unrelated work that has not reached the cache yet.
Clearing units/features no longer invalidates terrain textures.

The terrain regression verifies retention of unaffected chunks, correct corner
gutters, and an edit during background composition, comparing each result with a
full rebuild. Seven focused Release tests pass. The same terrain regression also
passes on Ultima Online B1 (63×63); this is correctness coverage, not a measured
FPS claim. Client and editor binaries were rebuilt. Whole-map edits still use a
full reset, and live minimap refresh/background thumbnail work remains pending.
The six editor tests and terrain regression also pass AddressSanitizer and
LeakSanitizer. The terrain harness now requests SDL's DBus shutdown on quit,
matching the editor test environment; its first leak run identified SDL's
otherwise-retained process-global DBus allocations.

## Region canvas checkpoint

The Regions toolbar button opens canvas authoring. Place drags out a uniquely
named rectangle; Select moves it or resizes its eight edge/corner handles. Enter
or double-click opens its name/bounds inspector. The region list also has an
Edit on Map button. Erase/Delete retains the existing referenced-region guard.
Choose another tool or press Escape to leave region mode.

Gestures preview without modifying the document until release. Escape,
right-click or focus loss cancels; movement preserves dimensions at map edges,
and resizing cannot invert the rectangle. Each completed gesture is one undo
step, and clicking a handle without movement does not create a history entry.
The seven Release editor tests pass, including a new SDL workflow that draws,
moves, resizes, cancels, undoes/redoes, renames and saves/reopens a region. Engine
CRT coordinates and trigger behavior are unchanged.

## Background validation and results checkpoint

Check Map runs against an immutable snapshot on a worker while the UI keeps
rendering. Escape discards its result when the current analysis finishes; quit
waits for that worker before normal shutdown. Results are a scrollable, wrapped
list: clicking a located issue centers the map and returns to the canvas. Scenario
> Validation Results reopens the list; it warns when the document revision has
changed since the check. Recheck is available in the panel.

Checks now diagnose duplicate/out-of-range/gapped start numbering, too few/many
starts, missing feature definitions, absent mana deposits, monarch approach
connectivity between starts/to mana, and rejected lodestone footprints. Route
queries call the engine's existing `World::pathExists`, including its nearby
endpoint resolution; these are approach checks, not claims that the exact
destination cell is occupiable. Island/transport scenarios receive warnings,
not prohibitions. Engine placement tests each allowed lodestone type at deposits.
Terrain and registered features are included; placed-unit occupancy, exhaustive
component analysis and naval output checks are still pending.

Eight Release editor tests pass, including a new UI workflow for asynchronous
validation, cancellation, clicking a located issue and reopening results.
Synthetic terrain tests exercise disconnected starts/mana and an unbuildable
deposit against the shared engine queries. All eight editor tests also pass
under AddressSanitizer/LeakSanitizer. The same validation UI workflow passes on
an Ultima Online B1 (63×63) snapshot. One Release run took 2.36 seconds end to end
with 823 MiB peak RSS, including fixture preparation, opening, two analyses and
shutdown; this is a workflow measurement, not the isolated validation cost.

## Generator recipe restoration checkpoint

Scenario > Regenerate from Recipe restores seed, player count, layout, densities,
dimensions and world. The settings dialog now exposes dimensions/world directly;
the preview shows the actual sanitized dimensions. Its Settings button returns
to those inputs for another preview. Accept replaces terrain/objects/starts/rules;
Discard preserves the current map. Regeneration of an existing document is one
undoable edit and preserves its authored name/description.

Recipes now have an optional `takgeneratorrecipe` OTA metadata field, independent
of editable descriptions. Existing editor description recipes and game-created
`.recipe` sidecars are imported. Malformed/non-roundtripping recipes fail visibly
instead of using the decoder's default seed. New maps, save/reopen, undo/recovery
and loose/KMP exports preserve the metadata. Reproduction assumes the same game
assets; the engine generator and simulation were not changed.

The generation UI workflow saves a generated map, edits its description, converts
the fixture to the game's sidecar storage form, resets the in-memory seed, reopens
and regenerates byte-identical TNT data. It then changes the seed and dimensions
through Preview > Settings and verifies save, undo and redo. All Release targets
were rebuilt after extending shared OTA metadata. Platform CI for the preceding
region/validation checkpoint passed on Windows, macOS and Linux.
The focused Release editor/map-transfer/generator/campaign suite passes (14 tests),
as do all eight editor tests under AddressSanitizer/LeakSanitizer.

## Correctable form errors checkpoint

Generator settings, region bounds/names, resize, Save As names, unit properties
and rule operands now show validation errors inside their forms. Values and
focus remain available for correction instead of closing the dialog. New-map
names are checked before either flat or random generation. Generator settings
are committed only after all fields, including dimensions, validate.

Changed unit numeric fields require complete whole numbers in their allowed
ranges; malformed/overflowing numbers no longer silently become zero or clamp
to another value. Bulk edits validate before changing any selected unit, and
unchanged authored fields retain their original values. Angle wrapping remains
available for positive/negative whole degrees.

SDL workflow coverage corrects invalid map names, generator dimensions, region
bounds and unit ownership without reopening the dialogs, then checks the saved
results. Filesystem/read failures still use separate messages; this checkpoint
does not claim every dialog failure path has been redesigned.
All eight editor tests pass in Release and under AddressSanitizer/LeakSanitizer.
A captured generator error dialog was visually checked: all eleven fields,
the wrapped error and both action buttons fit without overlap. The small bitmap
font remains a separate readability limitation.

## Recovery ownership and destination checkpoint

Each interactive editor now holds an operating-system lock for its recovery
session. Startup skips snapshots belonging to live editors, and a recovered
snapshot remains claimed until it is saved or discarded. Process termination
releases the lock automatically. Old flat recovery files remain supported;
new snapshots use isolated session directories. Cleanup deletes only the
session's snapshot, backup and lock, and reports deletion failures.

Recovery archives preserve the map name and absolute UTF-8 save directory.
Restoring uses that destination unless an explicit `--out` overrides it.
The save location is editor-only recovery metadata, not part of playable maps.

A real-process test keeps an autosaving editor alive, opens and closes a second
editor without a recovery prompt, kills the first editor, then restores its
unsaved edits and saves to the original directory. The test uses an isolated
installation so a destination regression cannot overwrite installed maps.
Asset-free coverage also checks exclusive claims, legacy recovery cleanup and
Unicode destination metadata. All eight editor tests pass in Release and under
AddressSanitizer/LeakSanitizer. Corrupt/truncated archives and additional
save/resize failure interactions still need broader coverage.

## Feature selection checkpoint

Features now support click/Shift-click and box selection in Select mode, group
movement, Ctrl+C/X/V/D, Delete, Enter for identifiers/cell coordinates, and
View → Frame Selection. Selected cells have visible outlines. Clipboard entries
store names rather than map-local numeric IDs, allowing paste across documents.
Moves and pastes reject the whole operation if any target would overwrite an
unselected feature, terrain marker or map boundary. Overlapping cells within a
moving selection are supported. Terrain heights/art remain untouched.

Selection resets on document replacement, resize and undo/redo. Gesture edits use the
existing history transactions. Tests cover feature-name remapping, protected
markers, atomic collision rejection, group dragging, deletion, undo/redo, save
and reopening. All nine editor tests pass in Release and under
AddressSanitizer/LeakSanitizer. Feature operations live
in a separate module; sprite-extent picking and a richer feature inspector are
still opportunities beyond the current cell-based selection.

## Layer visibility checkpoint

View now provides checked visibility controls for features, units, starts,
regions and the grid. All five settings persist between editor sessions.
Hidden feature/unit/start layers skip their canvas rendering and placement
previews. Direct selection, dragging and editing shortcuts are disabled for a
hidden layer; selecting its toolbar or palette reveals it again. The status bar
explains that behavior. These visibility settings do not change saved maps or
replace the separate terrain-brush object-protection option.

The feature workflow hides a selected group, tries Delete and Cut, reveals it,
and then verifies the existing move/undo/save/reopen sequence. Preference tests
round-trip every visibility flag. All nine editor tests pass in Release and
under AddressSanitizer/LeakSanitizer. The macOS recovery-checkpoint packaging
retry also passed; the earlier failure was a busy disk-image resource.

## Typed operand validation checkpoint

Check Map and the rule editor now share operand validation. Errors identify
player, rule group, condition/action and operand. Checks cover unknown types
and regions, empty flag names, malformed/overflowing integers, invalid display
player selectors, embedded NULs and CRT operand byte limits. Game-time values
that overflow the existing tick conversion are rejected. Out-of-range random
probabilities remain warnings because an always/never condition can be intentional.
The historical `Any Unit` placeholder is warned about rather than silently
rewritten; it is not a registered type in the current scenario interpreter.

The rule form validates a candidate before changing the document and keeps
fields available for correction. Signed flag/resource values and explicit plus
signs remain supported. Tests include partial numbers, double signs, integer
and tick overflow, player/type references and values that would truncate on
save. This adds editor diagnostics without modifying simulation semantics;
full opcode/operand execution parity and flag-reference conveniences remain
separate outstanding work. All nine editor tests pass in Release and under
AddressSanitizer/LeakSanitizer; Cartographer is rebuilt.

## Live background minimap checkpoint

Terrain painting now updates the visible minimap without saving. A 250 ms
quiet period combines quick strokes; a worker reads an immutable snapshot of
only the tile arrays and uses a separate decode cache. New edits cancel obsolete
work, and revision checks prevent an old result replacing the current preview.
Opening a document stops/joins the worker before replacing its borrowed VFS;
shutdown requests cancellation too. The status bar reports pending work/errors.

The 126×126 preview samples only the blocks used by the saved minimap's existing
nearest-sample rule. It does not scan every map block or build the larger saved
overview. Display pixels are kept separate from serialized terrain and history.
Save still regenerates the stored minimap/overview synchronously; asynchronous
save/export and thumbnail decoding remain outstanding.

The SDL workflow paints, waits for the real background result, undoes/redoes,
and checks both original-state recovery and exact RGBA parity with the saved
minimap. It verifies that a background update cannot mark the document dirty,
and checks cancellation before work starts. The ten-test editor suite passes
in Release and under AddressSanitizer/LeakSanitizer (with focused repeats after
the sampling optimization). The same workflow passed on the 63×63 Ultima
Online B1 map: 2.81 s end to end, peak RSS 964,612 KiB. This includes source
loading, reference-image construction, editor startup, three previews and saving;
it is not an isolated preview benchmark or a before/after memory comparison.

## Remaining work

1. Finish overwrite/resize interaction coverage and inspect corrupt/truncated
   recovery archives and additional cleanup failure paths.
2. Better font/Unicode, additional layout preferences,
   and verification of cross-platform packaging/launch usability.
3. Broader browser/selection interaction coverage,
   model previews where useful and additional view controls.
4. Placed-unit occupancy in overlays/validation, exhaustive land/water component
   and naval-output checks, and fuller resource checks.
5. Expanded typed-rule validation, rule names/objective templates,
   temporary-map playtesting through the normal client/server launch path.
6. Coalescing pending terrain work, asynchronous expensive operations, large-map
   profiling, broader document/UI coverage and final documentation cleanup.

No retail GUI launch is needed for the completed work. The separate untracked
retail-weapon probe is unrelated and is not part of this goal.
