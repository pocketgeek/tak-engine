# Territory selection, battle launch, and reporting lifecycle

Classification: **RECONSTRUCTED by static tracing**. All EXE addresses below
refer to EXE-3 and all Rover addresses to ROVER, fingerprinted in
[sources.md](sources.md). Original binaries and disassembly remain outside Git.
No retail UI or historical service was launched for this pass. This describes
client boundaries, not a replacement server protocol or recovered server rules.

## Territory selection reaches a battle area

The strategic-map hit-test at EXE `0x458e12` reads a 16-bit image-derived index,
masks its high bit, rejects the `0x7fff` sentinel, and indexes the territory
pointer array at `0x62aefc`. This is a screen-pixel-to-territory lookup, **not an
adjacency lookup**. The selected index is stored in map UI field `+0x1773`.

For the branch whose territory field `+0x49` is zero, the context menu binds
“Go to Battles” to `0x460d30` (`0x458ea6` and `0x458f27`). That handler sets
UI flag `+0x17eb`, then passes the selected territory index through `0x459550`
to `0x4609e0`. The latter takes the territory's ID field `+0x45`, stores it at
`0x62afe4`, clears `0x62afe0`, copies the territory name, and constructs the
war-console UI through `0x482db0` with arguments 1 and 3 (`0x460a25`–`0x460a6f`).
The console initializer copies the requested ID into session field `+0x95`
at `0x4836e5`. This traces the selected territory ID into the area-selection
state; it does not prove the server's rules for making a territory contested.

When the console's area-list callback has the requested ID available, it looks
it up with `0x42eda0`, obtains another identifier at list-entry offset `+0x27e`,
and calls area-entry routine `0x486ea0` (`0x4862f8`–`0x486341`). That routine
coordinates leaving/entering areas and uses Rover wrapper `0x5ab3fb`; the
callback arguments and UI state survive asynchronous entry. Thus the campaign
map does not itself select a local mission file and directly start combat.
It enters the corresponding Boneyards area and proceeds through battle entry.
The exact relation between both area identifiers remains a protocol detail,
not evidence that the file contains a server territory graph.

The independent recon request corroborates the server boundary. At
`0x460e2f`, the client sets `terr_id` from the same territory field `+0x45`.
It registers callback `0x45e970` under `metagame_mapinfo` at `0x460f4f`, sets
`type=mapinfo`, and sends `Crusades` / `terr_info_req` via `0x5ac155` at
`0x460fd9`. The callback reads `map_script` and `map_name`; their details are
in [battle-contract.md](battle-contract.md). The server-produced battle
`mission` is not yet proven to be a direct copy of either recon value.

## Creating/joining a battle and receiving launch settings

Two outgoing GameManager commands have been traced into Rover construction:

| Action | EXE wrapper / interface slot | Rover entry / message builder | Named message fields |
|---|---|---|---|
| Host | `0x5ab5a8` / `0x54` | `0x10003d33` / `0x10014a9b` | `command=host_game`, `area_id`, `type`, `name`, optional `password`, optional `description`, merged extra properties |
| Join | `0x5ab5fc` / `0x5c` | `0x10003e9e` / `0x10014b4e` | `command=join_game`, `area_id`, `game_id`, optional `password`, merged extra properties |

Interface assignments are at Rover `0x10001092` and `0x100010a0`.
Both entries remember completion callbacks at offsets `+0x37` and `+0x3b`
of the pending operation object, activate that object with `0x10007add`,
construct the command, and enqueue it through `0x1000df32`. Local success means
queued, not accepted. The EXE join call at `0x486ca1` supplies `0x48b070` and
`0x48b800`; when an area transition is needed, `0x48a5ef` performs the same
join after the transition callback. The host call sites `0x43d81d` and
`0x43dfe9` also supply `0x48b070`. The campaign branch of the console
(`0x4853ad` onward) constructs the host dialog at `0x43db60`; its resource
is `BYMetaBattleHostDialogue.gui` (`0x43db6a`), and its submit path reaches
`0x43dfe9`, passing host type 5. This identifies an actual campaign host call
site, without assigning untraced meanings to every other type value.

Crucially, Rover's response handler `0x1000aced` recognizes `launch_game`
(string at `0x10029904`). It requires the routing/connection fields `area_id`,
`game_id`, `address`, and `password`, matches the pending area ID, and, for a
join operation, matches the pending game ID (`0x1000ad7a`–`0x1000adb1`). It
reads `session`, `address`, `password`, and `result`, then copies the remaining
typed properties into a property object, excluding those routing/result keys.
The type dispatch at `0x1000af2e` handles `I`, `S`, and `U` values. At
`0x1000b143`–`0x1000b153` it invokes the stored six-argument callback with the
game identifier, session, address, password, result, and property object.
That is the callback signature consumed by EXE `0x48b070`.

On its successful branch, the EXE applies the battle properties documented in
[battle-contract.md](battle-contract.md), including mission, team, kingdom,
unit limit, visibility, and campaign-specific race restrictions. It forces
Crusades balance. At `0x48b6a1` onward it either leaves the enclosing area first
or constructs the battle transition directly (`0x48b707`, `0x48b714`). These
are server-supplied launch settings consumed by the ordinary engine, rather
than a local implementation of campaign progression.

This closes the static request/launch-callback link. It does **not** establish
that all named `host_game` types are valid in campaign rooms, authenticate an
incoming packet, or validate a replay against an original server. The title
screen-to-first-combat-tick DirectPlay lifecycle has not been dynamically run.

## Launch settings reach tactical session setup

Following the success callback beyond its transition closes the local session
handoff. These addresses all refer to fingerprinted EXE-3; this is static code
reconstruction, not a successful connection to the original Boneyards service.

| Received/local value | Staging storage | Consumer |
|---|---|---|
| `session` callback argument | `0x634b30`, copied at `0x48b623` | Host session setup through `0x49c410` |
| Local Boneyards player name | `0x634bd0`, copied at `0x48b634` | Host name argument at `0x4a48fa`; join player-name copy at `0x4a4c32` onward |
| Selected mission text | `0x634b70`, populated/cleared at `0x48b647` onward | Staging lookup through `0x4c7740` at `0x4a00be`–`0x4a00c9` |
| `password` callback argument | `0x634bb0`, copied at `0x48b674` | Host password argument at `0x4a48f0`; join password hash comparison at `0x4a4bf5`–`0x4a4c15` |
| `address` callback argument | `0x634bf0`, copied at `0x48b694` | Network address setup `0x4e89b0`, called at `0x4a48b3` and `0x4a496f` |

Transition `0x4a47b0` selects the create/join branches using staging flags.
The create branch supplies the stored name, session and password to
`0x49c410` (`0x4a48f0`–`0x4a48ff`). That routine copies name/session into
engine session fields, records the password hash/flag, calls `0x4e7400`, then
creates the local player through `0x4e71c0`. Session creation reaches
`0x544d30` through `0x4e7510`; that wrapper builds a size-`0x50` session
structure and invokes the network object's slot `+0x60` with flag 2 at
`0x544dad`–`0x544df2`. This is the create-session boundary, rather than a
campaign progression calculation.

The join branch initializes the same address, discovers the advertised session
through `0x5454a0` (`0x4a49f1`–`0x4a4a01`), checks its password and compatibility
fields (`0x4a4bee`–`0x4a4c2c`), and passes the discovered 16-byte session
identifier plus local player index to `0x4e78c0` (`0x4a4ca1`–`0x4a4cd5`).
Both branches subsequently construct the common tactical staging object with
`0x49f860` (`0x4a492e` and `0x4a4dc9`). Failed discovery/join also has a
leave-game path through `0x5ab64a` at `0x4a4d34`.

Thus `session` is consumed by session setup and `password` by session access
checks. Neither value is evidence of a cryptographic campaign-result token.
No server authentication, replay
validation, successful historical DirectPlay connection, or first combat tick
is established by this static handoff. Tracing every ordinary multiplayer
loading/UI step is unnecessary to infer the campaign arithmetic: it remains
on the server side of the already identified contract.

## Periodic versus final score reporting

EXE reporting dispatcher `0x500b40` takes an event value and a destination mask.
Its Rover branch requires the Rover reporting handle at `0x640590`, mask bit
0, enabled reporting-mask bits at `0x640594`, and the `0x424790` session check.
Report structures must already exist (`0x500b63`–`0x500b9e`). Event values
1, 6, 10, and 7 reset `0x640594` to all bits set at `0x500c37`.

| Event | Proven local behavior | Call-site evidence |
|---|---|---|
| 10 | Local player flag bit 3 gates an initial game/player metadata report; this path starts the periodic countdown at 60 | `0x500c8d`–`0x500f0d`; event call at `0x4a17b9` |
| 99 | Iterates the per-player score report only when that local flag bit is set; omits `last` | `0x500f2e`–`0x500f4d`; periodic caller `0x5011fb` |
| 7 | Sends the per-player score report only when the local winner field is nonzero, with `last=1`; then disables the countdown | `0x500f3c`–`0x50106d`; end-of-battle call at `0x52899e` |
| 8 | Bypasses the score-report loop and disables the countdown | `0x501063`–`0x50106d`; teardown calls `0x49eadd`, `0x52995c` |

Event 7's score loop has a further gate: the local player's **winner field**
must be nonzero (`0x500f17`–`0x500f47`). The pointer chain is verified by
allocation/initialization at `0x4ffd8a`–`0x4ffdaf`: each score entry's `+0x08`
points to an array of nine pointers, spaced eight bytes into its score record.
Array offset `+0x10` therefore points to record offset `+0x20`; the test at
`+0x04` reads record `+0x24`, exactly the field serialized as `winner` at
`0x50100a`–`0x50101b`. This is the local reported winner value, not proof that
the server agrees with it. Losing clients do not traverse this final-report
loop merely because event 7 occurs. The event-7 call is directly before the
end-of-battle transition and screen construction (`0x5289a3`–`0x5289bf`).
Event 8 is also present in cleanup that explicitly invokes the separate
leave-game wrapper `0x5ab64a` (`0x49eb15`). It is not proof of a server-side
surrender-forfeit rule.

The periodic dispatcher `0x5011d0` decrements session field `+0x3e` only while
nonnegative and the session check succeeds. At zero it calls event 99 and
resets the field to 60; events 7 and 8 set it to -1. The unit here is
**dispatcher invocations**, not established wall-clock seconds. The score
payload and enqueue/socket boundary are documented separately in
[battle-contract.md](battle-contract.md) and [report-transport.md](report-transport.md).

## Authority and unresolved outcomes

The client supplies tactical player statistics, a normalized winner value,
and a final marker. It does not send a recovered numeric fatigue delta,
support delta, toughness update, or authoritative territory owner in this
score path. The primary shipped FAQ explicitly attributes campaign arithmetic
to the servers; see [campaign-rules-evidence.md](campaign-rules-evidence.md).

The observed `launch_game` response is evidence of battle-entry response
handling. It is **not** a score acknowledgement. Score acceptance, duplicate
suppression, loss/abandonment credit, disputes between participants, rank
weighting, orphan-battle storage, and ownership mutation remain UNKNOWN at the
server boundary. Recovering a client command with `last=1` does not resolve
those questions. A replacement service must label any chosen policy as new
behavior until original server code, traffic, or equivalent evidence appears.
