# Construction, cursors, and building ruins (2026-09-27)

## Construction-site clearing

A mobile reclaimer's Build command now admits a site blocked only by reclaimable
features and queues their reclamation before construction. This is an engine
convenience beyond retail. Previously the client sent reclaim orders followed by
a Build command, but the simulation discarded the Build while the obstacles
were still present.

Placement and clearing share terrain, yard-map, mana-site, footprint, and unit
occupancy checks. Navigation stays blocked until reclamation finishes. Shift
queues and build lines use the same authoritative path; Stop cancels the work.
Unreclaimable decorations are excluded even when registered for burning.

`clear_build_test` exercises ground/flying builders, both balance modes, queued
movement, cancellation, complete construction, and invalid sites.

## Cursor and construction-assist audit

The offline probes execute installed retail routines without launching the game:

- `probe_cursor_assist.py`: selector `4dd780` with actual `519d40`/`519e60`
  eligibility. Unfinished construction requires an eligible builder, the same
  owner, and a matching build option **only when `builderlimited` is set**.
  Build-menu membership is a controlled input to this probe.
- `probe_cursor_action_modes.py`: revive, transport load/unload capability and
  selected-transport count. Cell validity helpers are controlled inputs.
- `probe_cursor_findsite.py`: live builder/build-list requirement.
- `probe_cursor_airstrike.py` and `probe_cursor_airstrike_active_weapon.py`:
  authored dropped-weapon flag and active-slot selection.
- `probe_cursor_native_restart.py` and `probe_cursor_legacy_start_phase.py`:
  native registration, frame phase, and cursor switching. Synthetic sequence
  records isolate the manager behavior; no retail pointer or renderer is driven.
- `cursor_test`: all 21 registered cursor assets, frame timing, and weapon-range
  decisions supported by the engine.

The client now checks actual assist and boarding eligibility, rejects armed
commands with incapable selections, checks repair/guard/reclaim targets, and
uses the same screen-space unit bounds for contextual clicks and hover feedback.
Damaged friendly units advertise repair; eligible corpses advertise revive or
reclaim. An unrestricted builder's assist command now follows retail instead of
being incorrectly rejected merely because the target is absent from its menu.

`TAK_CURSOR_TEST=1` with the Debug client's local `--testbuild` harness exercises
actual hover selection, restricted/unrestricted construction, right-click assist
and repair, incapable command cursors, building hit bounds, and sparse result
rosters. This is an engine regression check, not a retail GUI comparison.

Capture/Pickup/Teleport remain registered retail placeholders; this work does
not invent gameplay commands for them. Remote/dropped weapon classes retain the
existing conservative range handling described in `cursorrange.h`.

## Building death and ruins

`probe_native_set31_lifecycle.py --native-death-state --instant-corpse` executes
native Killed dispatch, EXPLODE dispatch, owner retirement and destruction. Map
stamping/effect allocation are controlled seams. All sixteen recognized building
scripts retire immediately and request corpse 1 for both ordinary and explosion
damage: 32 native cases. They include arakeep, aracastl, arangate, creacad,
cregate, cresmit, npctemp, npctemp2, tarcastl, tardung, tarhell, tarngate,
vercastl, verfltwr, verkeep, and verngate.

The engine recognizes their constant, bitmap-only Killed handler and absence of
a Dying callback; it does not hardcode the unit names. Other script forms keep
the existing death-animation lifecycle. These buildings now display/block their
ruin immediately, preserve it after explosion damage, and remove attached owner
smoke at retirement. The ruin renders in its own rest pose.

`probe_corpse_origin.py` executes native corpse-placement argument routing and
the feature position/orientation copy. `corpseadjustx/z` changes the map footprint
anchor, **not** the model position. The ruin retains the original position and
orientation even when its footprint size differs. `building_death_test` checks
both balance modes, immediate ordinary/explosion deaths, model position and
blocking. This corrects the old visual offset while preserving map-cell rules.

## Results

Skirmish/multiplayer results use the frozen starting roster, preserving defeated
or disconnected participants and excluding open/closed slots. The generic
“Enter: Continue” hint is removed; campaign next-mission hints remain.
