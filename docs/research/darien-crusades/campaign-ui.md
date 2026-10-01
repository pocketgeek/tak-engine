# Milestone 9: strategic campaign screen

`CrusadesScreen` provides a strategic view over the authenticated campaign
service. It is a modern interface around the recovered campaign data and
server-authoritative duel flow, not a claim to reproduce the original Boneyards
lobby. The screen labels the remaining limitation: **Historical capture rules
incomplete**. The interface is available from the authenticated multiplayer
game browser through **Darien Crusades**. Milestones 10 and 11 extend this screen
with [modern territory FIFO matchmaking](campaign-matchmaking.md) and
[verified terminal history and retained tactical replays](campaign-history.md).
The current subtitle identifies modern FIFO duels and incomplete historical
capture rules.

## Navigation and territory inspection

The catalog selector cycles campaigns in the current page; right-click cycles
backward. More/First page browses the paginated server catalog. Refresh requests
fresh catalog, selected campaign and own status. Search matches territory names
or IDs; the territory list scrolls independently and exposes every server
territory, including parcels without a mapped local shape. Clicking either the
map or a list row selects the same territory.

Details show server ownership, open/active battle counts, native faction,
terrain, effective battle map, all seven recon metrics and authored neighbors,
plus authoritative matchmaking availability and waiting counts for each alliance.
Missing fields display **Unknown**, distinct from zero or an explicitly empty
neighbor list. Activity counts come from the server's current rooms, not from
battle-point inference or decorative fires. Original README-3 describes flames
based on territory player counts on the full map and minimap; modern offered/active
room counts do not reproduce that display. Own recent battles are shown
separately. Long descriptions and neighbor lists wrap in a clipped, scrollable
detail pane.

The renderer loads strategic assets only from the user's VFS. Retail Darien
artwork is used only when the complete authoritative territory ID/name set
matches the local definition. Ownership colors come exclusively from the
snapshot. Unmapped/unknown areas remain neutral; native faction is never
substituted for ownership. The local territory description can supplement a
matching ID/name, without overriding server fields.

Without compatible assets, the view is explicitly labeled **Modern schematic -
not geography**. Its deterministic tile layout supports selection but does not
invent a Darien coastline, adjacency graph, or map assignment. Retail fonts are
used when available; otherwise a readable block font transliterates common Latin
accents. Both routes fit/clip labels to their panels. Original assets remain
outside the repository.

## Allegiance and battles

Honor/Terror actions use the latest server allegiance revision (or the absence
sentinel when joining). They do not locally predict a successful switch. A
battle request requires a selected territory with an assigned/authored map,
confirmed enrollment, a valid opponent account and the client's Lobby state.
The server still performs every authority and eligibility check. No AI or
client-specified winner/map/rules capability is introduced.

**Find opponent** searches the selected territory when the authoritative board
confirms both player and territory eligibility at the displayed campaign revision.
The server pairs opposite alliances in arrival order for that campaign and
territory. The older waiter hosts the room and the other player receives an
invitation. **Cancel search** waits for server confirmation; neither action
predicts local queue counts or battle issuance. Direct **Request battle** by
opponent account remains available through the same issuance checks. The
[M10 contract](campaign-matchmaking.md) records expiry, reconnect, reservation
and eligibility policies.

Own battle references trigger bounded status queries as their lifecycle changes.
The list shows invitations, running/results-pending battles and terminal status;
completed results identify the authoritative winner when present. Join
Invitation uses only an issued battle's live nonzero room ID. Leaving a battle
uses the existing room action. The host's already-seated issued room and actual
battle/lobby flow remain the containing GameView's responsibility.

## History and replay playback

**History** opens the selected territory's verified terminal results for an
enrolled account. Previous/Next paginate the archive; scrolling browses entries
and clicking one shows its recorded outcome, participants, map, date and statistics.
Missing recordings leave history visible. **Watch replay** downloads an available supported recording
only while the player is in the lobby, outside a search or active battle. The
client verifies the recording digest before handing it to the existing tactical
viewer. Cancel download discards an unfinished transfer. Returning from playback
preserves the campaign connection, selected territory and history selection.
See [M11 archive access, retention and playback](campaign-history.md).

This tactical archive is modern. The original help describes a Crusades movie
showing battle lines and conquered territories; that strategic history display
does not establish the modern battle archive or replay-access policy.

## Networking and reconnect

The screen never calls socket `poll`, accesses a simulation `World`, modifies a
snapshot or records a tactical command. `MpClient` owns networking and cache
validation. Disconnection exposes Sign in again and Back; the parent flow
restores the campaign subscription and, once its snapshot arrives, a valid
selected territory. Unauthenticated and unavailable-service states do not issue
repeated requests every frame.

## Evidence and limits

The original GUIs group a strategic map, territory details, gatherings and
people/battles navigation; the recovered host dialog included two/four-player
choices. This implementation deliberately exposes the existing modern two-player
authenticated duel service. See [network snapshots](campaign-network.md),
[territory policy boundaries](campaign-territory-rules.md), and
[retail campaign flow](campaign-flow.md). A local map image does not make the
unknown historical capture or campaign-victory calculations available.
The [Milestone 12 historical validation](../../darien-crusades-reconstruction.md)
compares current presentation and service choices with that evidence.

## M9 focused validation record — 2026-09-30

The following 57-check UI and full-suite counts record the M9 implementation
before FIFO matchmaking and history/replay were added. Current extension checks
and workflows are recorded in the [M10](campaign-matchmaking.md) and
[M11](campaign-history.md) validation sections.

`tools/crusades_ui_test.cpp` uses software SDL, a real `MpClient`, real SCRAM
mutual authentication and a synthetic 40-territory server snapshot. Its 57 checks
cover list/search selection, logical-coordinate resize handling, parent renderer
scale/clip preservation, missing-map/enrollment action gates, expected-revision
allegiance requests, the exact selected duel target, invitation joining,
unchanged authoritative data, and disconnected/back actions. The room-binding
regression confirms terminal room-ID-zero notifications do not unlock the
existing campaign lobby. No retail assets
are needed. `TAK_CRUSADES_UI_SCREENSHOT=/tmp/preview.bmp` optionally saves the
synthetic screen for inspection; that adds one preview-write check.

The production UI, map, font, client and codec paths also pass ASan/UBSan with
leak detection in all 57 focused software-renderer checks; supporting framework
libraries were not instrumented. The final screen, UI test and font sources
also pass strict MinGW object compilation using vendored SDL headers; that is
not a Windows runtime test. Full application/network validation and the final
Release/Debug suite results are recorded below. The map loader's separate tests establish asset parsing, compatibility,
composition and hit testing; UI tests do not substitute synthetic geography for
retail evidence.

The application UI smoke is registered for Linux Debug builds with game data,
where SDL preferences can be isolated through `XDG_DATA_HOME`; the synthetic SDL
UI test runs on all supported platforms. Native Windows/macOS runtime checks
remain separate from the successful MinGW compile checks.

## Server bootstrap

Build the `crusades_import` target, then import the metadata from your own assets:

```sh
build/crusades_import /path/to/Darien.def darien /path/to/darien.campaign
```

The output is a modern campaign definition with the original territory IDs,
names, descriptive native races and terrain. It refuses an existing output path.
Keep this asset-derived file with local game data, outside the repository. It
contains no inferred owners, adjacency or maps. Append explicitly authored
`map <territory-id> "<installed map identifier>"` directives for territories you
want to use for battles. Imported ownership remains unknown; the definition
does not recover original service state. Enable the server with
`--crusades-db /path/to/campaign.sqlite --crusades-definition /path/to/darien.campaign`
and its normal authenticated account configuration. See
[battle setup](campaign-battles.md) for policy and account details.

The local art loader parsed all 313 installed territories at 1672×1083 and
mapped every territory's fire-anchor seed. Fifteen disconnected magenta pixels
remain neutral and unselectable; the territory list still exposes every parcel.
Synthetic tests cover the bounded CP1252 definition parser, PNG checksums,
filters, indexed formats, ownership composition and geometry hit testing.
Existing static zlib handles PNG inflation; no new dynamic library is required.

## M9 acceptance evidence — 2026-09-30

| Criterion | Evidence |
|---|---|
| All current territories displayed | 313-territory actual GameView overview/list; synthetic 40-territory scroll/search UI checks |
| Server ownership and activity | Three ownership-composition cases and unknown gray; live offered/started/terminal room counts without invented capture |
| Territory details | Actual list and map clicks, server metadata/recon fields, optional local descriptions, unknown-versus-zero checks |
| Eligible battle entry | Actual authenticated invitation selection and SDL Join action reaches the existing server battle room |
| Reconnect restores state | Fresh authenticated snapshot/allegiance/battle recovery; same server/account subscription and territory restoration in main flow |
| Historical/modern boundary explicit | Matching user-local map geometry, labeled schematic fallback, modern duel subtitle, incomplete capture notice |

The final full Release sweep passed all **143 tests**; Debug passed all
**149 tests**, including the actual application UI/network smoke. Presentation tests passed **73 checks**,
protocol tests **722**, service tests **57**, and SDL UI tests **57**. Focused
ASan/UBSan/leak runs passed for the codec and production UI/map/client paths;
strict MinGW compilation passed for the new map, UI, font, importer and tests.
Supporting unchanged framework libraries in these sanitizer runs were not all
instrumented. Native Windows/macOS UI runtime execution is not claimed.
The existing deterministic math guard passed. No tactical simulation source was
changed, no retail process was launched, and no new dynamic dependency was added.
