# Area-clear target selection — 2026-09-26

The client used to collect the visible features and corpses in a dragged box,
sort them once by floating-point distance from the builder at click time, and
send one queued reclaim command per target. That order becomes wrong as the
builder walks, and cannot include targets that become available during the job.

Area clearing now sends one persistent `ReclaimArea` command. The simulation
approaches the nearest point of the rectangle through its existing movement
controller, selects one eligible target, and runs the existing reclaim child.
When that child finishes or its target disappears, it selects again using the
builder's current position. Other queued orders remain behind the area job;
Stop and replacement orders cancel it. The mobile infinite-production lock
continues to apply through the shared command dispatcher.

Selection samples the rectangle every 16 world units, in increasing Z then X.
It compares the sum of the high 32 bits of each squared signed 16.16 coordinate
difference, retaining the first sampled cell on equal distance. Multi-cell
features resolve through their footprint back-references, so a box touching
only a footprint tail can select the feature. Reclaimable corpse anchors use
the same cell lookup. Eligibility is recomputed at each selection, not frozen
at click time.

The native evidence comes from static inspection of retail `KINGDOMS.icd`:

- `0x406670` (ReclaimArea): stage 1 clamps the builder's position to the
  rectangle; stage 2 calls the selector and creates a single reclaim child.
- `0x509cc0`: rectangle sampling, eligibility/visibility calls, signed fixed
  distance ranking, strict replacement on smaller distance, and scan-order ties.
- `0x509e6f..0x509ece`: distance and winner update.
- `0x509ed1..0x509eff`: 16-unit X and Z increments.

`tools/re/check_reclaim_area.py` executes the original selector under native
instruction emulation with controlled eligibility and visibility inputs. All
1,024 randomized cases matched the production helper, including fractional
rectangle origins, fractional builder positions, ties, and empty cells. No
retail game GUI was launched. The probe does not claim to validate the native
eligibility/visibility implementations it substitutes.

Authoritative selection cannot read the client's asynchronous display fog.
It reconstructs current allied visibility using the existing retail sight
footprint routine and immutable terrain, identically on server and client.
Unseen targets are refused. This deliberately retains the project's rule
against targeting objects in fog: retail's selector also contains a special
explored-object allowance which is not newly enabled here. Live GUI parity of
that visibility distinction, approach animation timing, and individual reclaim
work timing is outside this target-selection fix. The existing pathfinding and
individual reclaim controllers are unchanged.

`reclaimarea_test` covers dynamic selection, completion, queuing, cancellation,
a target removed by another builder, hidden-target refusal with no display fog,
allied sight, footprint tails, command framing and truncated endpoints. It is an
unconditional CTest and needs no retail assets.

Network protocol **182** gates the new authoritative command and state. Only
`ReclaimArea` appends its second rectangle endpoint to the normal command bytes;
ordinary commands retain their 35-byte layout. Mixed command streams round-trip.
Old peers/replays are rejected by the existing protocol-version check. Area
bounds and approach state are hashed only when an area order is present, so
matches that do not issue this command retain their existing state hashes.

## 2026-10-24: retail exits, explored fog, removed map features (protocol 294)

Three gaps made real area clears stall or skip trees. All three now follow
retail (static reading of `KINGDOMS.icd`; the fog rule also emulated):

- **Exits (`0x406670`, `0x406250`).** Stage 0 records the builder's position,
  when the order first runs, as the last target (+0x22). Stage 1 pushes its own
  approach leg (`Order::reclaimAreaApproach`): a radius-4 circle on the clamped
  point, wait mask 0x700, installed even from inside the box. It ends on arrival
  (0x500) or failure (0x200, a zero-point route delivery while unsatisfied) and
  never widens like Move_Ground. Stage 2 picks: no target ends the order (5); a
  pick snapped into the same `0x50e660` cell as the last target retires it (8);
  otherwise the snapped pick becomes the last target and a child RECLAIM runs.
  The child ends when its approach fails (0x200 while out of reach,
  `0x40638e -> 0x4062a5`). So an unreachable nearest target fails its child,
  gets re-picked, and the whole order retires. A first pick in the builder's own
  cell also retires at once (a retail quirk). `retailReclaimAreaPick` is the
  pure stage-2 helper. The area order itself never routes or steers: the mover
  brakes while it is current.
- **Fog (`0x509d9f..0x509e6d`).** An unseen sampled cell still counts when its
  +7 owner byte is 0xa (a world feature) and the cell is explored. Corpses carry
  their dying unit's owner (`0x512ee0`), so a fogged corpse anchor is refused but
  its tails (0xa) are not. With line of sight off, only explored counts. The sim
  reads its own hashed Mapping plane for the builder's player
  (`navigationExplored_`; retail reads the local human's bit, which is not
  deterministic), plus the room's FOG OF WAR option (`World::setRoomVision`:
  EXPLORED reveals the map, FULL VISION also turns LosOn off; campaign `.ota`
  mapping/lineofsight) and the power codes. Replay format 15 records the option.
  A scratch emulation probe (not in the repo) ran the real `0x509cc0` with
  `0x50e660`/`0x4223f0` over 4,000 cases against this rule.
- **Removed map features (`0x495360`, `0x496380`).** When two TNT features
  overlap, the earlier in scan order is removed at load and retail keeps nothing
  of it. `World::installedMapFeatures()` (and the World-free
  `retailInstalledMapFeatures`) give the survivors; matchsetup registers only
  those, so the others make no art, blocker, sacred site, deposit or target.
  Emulating the native install loop and draw scan matched the plane on 1,360
  maps (481 of them lose 18,660 features; Inner Circle loses one zontree202).

- **Every selected reclaimer (`0x5214b0`, client only).** Retail resolves each
  selected unit through `0x4de530` (action 0xc); a type with `+0x264` bit 0x800
  gets RECLAIMAREA through `0x4d78a0` with the same rectangle, any other gets
  no mission and keeps its queue. A click gives every reclaimer RECLAIM on the
  same point (the mission record has no formation bit). Without Shift each
  unit's own queue is replaced (`0x4d6a50`). `GameView::reclaimers()` and
  `issueReclaimBox` / `issueReclaimFeature` now send one command per reclaimer;
  nothing claims or splits the box, so builders can converge on one tree as in
  retail. Not ported: an armed CLEAR left-drag as an area clear, and the Shift
  toggle that removes an already queued click (`0x4d7a30`).

Still ours, not retail: reclaim starts within `24 + 8*foot + builddistance` of
the target, where retail's child approach is `0x4d4e80`'s footprint-adjacency
rectangle with no builddistance; and work runs at 120/s against max(energy, 60),
where retail's countdown is about energy + 15 ticks (about 4x slower for a
250-energy tree). Neither is emulated yet; both would change every reclaim.
