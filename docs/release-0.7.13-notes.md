# TAK Engine 0.7.13

Cartographer now has a complete desktop editing workflow: searchable browsers,
undo/redo and recovery, background saves, map validation, scenario tools and an
engine-backed Test Map handoff. Diplomacy hides gifting and mana-sharing controls
for non-allies.

## Cartographer

- Desktop launchers across the packaged platforms, remembered data location and
  window preferences, scalable text, menus, recent maps and searchable opening.
- Background KMP saving and loose export, safe replacement/rollback, periodic
  recovery snapshots, undo/redo and explicit checks before cropping maps.
- Searchable terrain/unit/feature browsers, asynchronous thumbnails and art,
  group editing, named placed-unit lookup and an F6 rotatable model inspector.
- Engine-backed terrain/placement overlays and navigable map checks, including
  connectivity, missing resources and initial naval production clearance.
- Reproducible random-map controls with preview before adoption, and cached,
  asynchronous terrain/minimap work for large maps.
- Named regions, typed rule operands, rule clipboard/reordering, objective
  templates and full rule-text inspection.
- F5 launches a temporary map snapshot in the normal private client/server lobby.
  Scenario companions are transferred and verified, with optional local trigger
  execution logs. Editing and testing do not require saving over the source map.

Full retail mission-runtime parity is not claimed. Unsupported authored records
are preserved, and current execution limits are documented in the trigger guide.
The model inspector previews visuals; it is not a full unit-behavior simulator.

## Compatibility

Protocol **198**: update clients and servers together. No new dynamic runtime
libraries are required. Retail assets remain external and are not distributed.

See the [validation report](https://github.com/pocketgeek/tak-engine/blob/main/docs/release-0.7.13-validation.md)
and [Cartographer guide](https://github.com/pocketgeek/tak-engine/blob/main/docs/user-guide.md#map-editor).
