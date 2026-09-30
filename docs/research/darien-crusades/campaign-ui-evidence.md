# Campaign interface evidence and implementation boundary

The Crusades interface combines recovered presentation assets with a modern,
authenticated campaign service. It is not a reconstruction of the retired
Boneyards service or its complete social interface. Historical ownership and
capture calculations remain unknown where the server data is missing.

## Evidence inspected

This review used the existing GUI parser on the locally extracted
`meta.hpi` campaign dialogs, and the Milestone 1 fingerprinted asset and native
code investigations. No retail executable or historical service was launched.
Original asset bytes, full dialog text and extracted widget tables are not
committed. Paths below identify resources in a legally obtained installation.

| Resource | Confirmed presentation structure |
|---|---|
| `guis/BYMetaConsole.gui` | A 640×480 console with the strategic map on the left, an overview and selected-territory information on the right, and navigation below |
| `Anims/BYMetaConsole.GAF` | `BYMetaMapBG` background; `BYExitButton`, `WarRoomButton`, `NnIButton`, `HelpButton`, `HouseButton`; smaller `PeopleMetaButton`, `BattlesMetaButton`, `LocatorMetaButton`, `MovieButton`; `MapBorder` and `Thumbtack` |
| `guis/BYMetaWarConsole.gui` | Chat area on the left; people, gatherings, battles and locator controls on the right; navigation below |
| `Anims/BYMetaWarConsole.GAF` | `BYWarBG` and corresponding navigation/tab sequences |
| `guis/BYMetaBattleHostDialogue.gui` | Host dialog with game name, optional password, two/four-player choices and a persistent-units control |
| `Anims/BYMetaBattleHostDialogue.GAF` | `HostBG`, `CancelButton` and `OKButton` |
| `Boneyards/Metagame/Darien.def` | Territory identities, names, descriptive native races/terrain and presentation anchors; not a complete server campaign configuration |
| `Boneyards/Metagame/Borders.png`, `HonorMap.png`, `TerrorMap.png`, `ContestedMap.png` | Strategic-map geometry and state artwork; local copies match the independently fingerprinted GOG distribution |

The GUI records parse using the existing numeric-stream parser. That establishes
resource references and layout, not every native gadget behavior. The earlier
[GUI validation](numeric-gui.md) records hashes and aggregate parser counts.
[Distribution provenance](distribution-provenance.md) distinguishes independently
matched artwork from its unresolved original patch/CD delivery route.

## Geometry and labels

The map images are 1672×1083. Darien.def's header labels those axes in the
opposite order; image dimensions must govern image coordinates. The recovered
client fills connected border-image regions from parcel fire anchors and builds
a reduced pixel lookup containing parcel vector indices. A clicked pixel can
therefore select a parcel. Neither that fill nor visual contact establishes
server adjacency. See [territory geometry](territory-parameters.md#how-the-client-obtains-clickable-territory-shapes).

The definition has 313 parcels with noncontiguous identifiers. Native race is
descriptive: Aramon, Taros, Veruna, Zhon or Neutral. It does not establish the
current owning alliance. Honor, Terror and contested are ownership states;
unknown ownership must remain visibly unknown instead of being silently treated
as contested. `PreInit.jje` is not proven to supply authoritative starting
ownership for a live campaign. See [definition evidence](territory-format.md).

The recon presentation distinguishes required, fatigue, support and battle
victory points. Missing values are unknown, not zero. Local templates explain
capture in terms of these values and momentum, but do not recover exact server
arithmetic. Native race, ownership, map assignment and recon metrics must remain
separate fields. See [parameter evidence](territory-parameters.md).

## Modern flow versus recovered behavior

The native map action enters a server-controlled territory area and then the
war console. The server supplies battle launch settings; the map does not
select an arbitrary local skirmish file and start it directly. The static path
is documented in [campaign flow](campaign-flow.md).

The current engine's catalog, authenticated allegiance choice, explicit
opponent request and two-player battle room are a modern service interface.
The fixed Aramon/Taros duel setup is not evidence of a historical faction ban.
The retail host dialog's four-player and persistent-unit controls are evidence
of a broader historical interface; displaying those controls without supported
server behavior would be misleading. Likewise, chat, houses, historical rankings,
news and password-protected campaign battles are not implied by reusing console
artwork.

Territory activity reports currently offered and active rooms from this server.
These are modern, volatile observations, not historical traffic values. They
can change without changing the persisted campaign revision; the client accepts
activity-only updates in ordered network responses while still rejecting
conflicting ownership, metrics or definition fields at the same revision.

Local geometry only supplies presentation. The authoritative snapshot supplies
campaign identity, territory state and revision. A missing geometry resource,
missing map assignment or unknown historical rule must not invent an owner,
adjacency link, battle map or capture result. Recoverable presentation and
unavailable campaign arithmetic are separate concerns.

## Validation boundary

Milestone 8 already tests authenticated requests, subscriptions, full snapshots,
participant-only lifecycle messages, durable results and reconnect behavior.
Milestone 9 adds interface-level checks. On 2026-09-30,
`tools/crusades_ui_network_test.py` ran the actual Debug `takclient` GameView
against the Release campaign server using SDL dummy video and software rendering.
Mouse events selected Honor, switched to Terror after reconnect, and joined an
invitation from another authenticated account. The test checked the persisted
allegiance revisions and actual server room-join log; it did not substitute raw
allegiance requests for the screen's input handlers. Three 960×540 captures were
produced and inspected, including the strategic screen and resulting battle
lobby. The synthetic campaign deliberately exercised the explicitly labeled
schematic fallback rather than inventing Darien geography for unrelated IDs.

The run passed. Local artifacts were written under
`/tmp/tak-darien-m9/ui-live`; no original asset bytes or captures were committed.
The existing real-server protocol test additionally verified offered, active and
terminal territory counts, and confirmed that campaign rooms are absent from
the ordinary public game browser. These observations do not claim the entire
historical Boneyards UI or an end-to-end tactical match was exercised through
mouse input; authoritative tactical completion remains covered by the separate
result/network tests.

A second actual-client run imported the local Darien definition with
`crusades_import`, yielding all 313 territories. It initially exposed a real
integration omission: the standard retail VFS did not mount the shipped loose
strategic resources. After adding the narrowly allowlisted cosmetic resource
mount, the repeated run displayed the Darien border geometry and neutral unknown
ownership. Selecting Torcairn displayed territory 8193, its descriptive native
Aramon faction and Hills terrain, while ownership, battle map, recon values and
neighbors remained unknown. Captures are under `/tmp/tak-darien-m9/ui-darien`.
The VFS regression separately checks that these presentation resources do not
alter the gameplay fingerprint or expose adjacent gameplay files.
A separate SDL click inside the displayed map selected Whisper Hills (8194) and
updated the details panel, exercising the geometry hit-test through GameView
rather than selecting only from the territory list.
