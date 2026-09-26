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
