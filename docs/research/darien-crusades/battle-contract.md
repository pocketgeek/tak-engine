# Battle settings and score-report boundary

Classification: **RECONSTRUCTED by static tracing**, unless noted. Addresses use
EXE-3 and ROVER from [sources.md](sources.md), not the differently hashed local
engine. No game, installer or network service was launched. This is research,
not an implemented campaign protocol.

## Settings passed into battle entry

EXE-3 callback `0x48b070` consumes a property object on its success branch.
The following table records reads and local initialization values. Those values
are **not** claimed to be server policy, valid ranges, or final UI enum meanings.
Some settings are translated before being stored in the client's global state.

| Property | Read location | Local value before read / special handling |
|---|---|---|
| creator | `0x48b1d8` | Failed read explicitly sets zero |
| playerlimit | `0x48b1f6` | 2 |
| lockOptions | `0x48b208` | 0 |
| max_units | `0x48b21e` | 200 |
| deathopt / on_death | `0x48b242` | 2; key depends on creator value |
| los | `0x48b259` | 1; incremented before storage |
| map_revealed | `0x48b26f` | 2; incremented before storage |
| cheats | `0x48b281` | 1 |
| location | `0x48b293` | 0; incremented before storage |
| watching | `0x48b2b6` | 0 in campaign branch, 1 otherwise; incremented before storage |
| tournament | `0x48b2c8` | 0 |
| team | `0x48b2de` | 5 |
| kingdom | `0x48b2f0` | 1 |
| game_type | `0x48b302` | 0 |
| mission | `0x48b320` | String; copied to battle settings at `0x48b647` onward |
| map_races_allowed | `0x48b43b` | Campaign-specific race eligibility string |

Territory callback `0x45e970` separately reads `map_script` and `map_name` into
territory string fields at offsets `0x1f3` and `0x1d3` (`0x45e9d2`, `0x45e9ef`).
This establishes two named fields, not the syntax or executable meaning of
`map_script`, nor a proven conversion from it to the battle's `mission` value.

## Race restrictions

The campaign branch reads `map_races_allowed`. If the property is found, it
initializes five restriction flags to one. It then scans the supplied text.
For `team == 1`, it recognizes H followed immediately by a race letter; for
other team values, it recognizes T followed by a race letter. Matching letters
clear the corresponding flag. Missing property and an empty supplied string
therefore take different paths: the former leaves the initially cleared flags;
the latter leaves all five set. A modern implementation should not blindly
copy this permissive missing-field behavior as a security policy.

| Letter | Flag address | Interpretation |
|---|---|---|
| A | `0x634c65` | Aramon; corroborated by recon display's race text |
| T | `0x634c66` | Taros; corroborated by recon display's race text |
| V | `0x634c67` | Veruna; corroborated by recon display's race text |
| Z | `0x634c68` | Zhon; corroborated by recon display's race text |
| C | `0x634c69` | Fifth slot; expansion-race interpretation not fully traced in this pass |

Dispatch tables at `0x48b780` and `0x48b7b4` have the same letter mapping.
The scan at `0x48b487`–`0x48b505` is case-sensitive. It is a scanning loop,
not a validated delimiter grammar. Do not infer separators, canonical ordering,
or robust malformed-input handling from these two-character tokens.

At `0x48b507` onward the client advances its selected kingdom past restricted
slots. Other lobby paths read the same restriction flags. The recon display
has a separate H/T scanner at `0x45ea63` onward. Together these corroborate
territory-supplied race permissions, rather than fixed Honor/Terror race lists.

## Crusades balance

`game_type` is mapped into the mode byte at `0x634c64`; the campaign branch then
forces that byte to one at `0x48b3ef`. During load, `0x527621` converts this byte
to a boolean and passes it to `0x5174c0`, which stores the balance selection at
`0x641144`. The same loader handles the ordinary game settings byte named
`CrusadesBalance` (`0x49978d`). The selected data path includes `UnitsCB` (string
at `0x618870`) and the corresponding FBI glob (`0x618850`).

This supports mandatory Crusades tuning in campaign battles. It does not replace
the still-outstanding per-unit balance comparison or establish every effect of
all nonzero mode values.

## Score report: EXE to Rover

A report path at `0x500f76` iterates player entries, creates a property object,
and populates these keys using `0x5abb55`:

| Key | Setter call |
|---|---|
| kills | `0x500f94` |
| losses | `0x500fa3` |
| total_mana | `0x500fb2` |
| mana_wasted | `0x500fc1` |
| current_units | `0x500fd0` |
| peak_units | `0x500fdf` |
| score | `0x500fee` |
| total_score | `0x501005` |
| winner | `0x50101b` |

The winner value is normalized to zero/nonzero. **Neither score field is proven
to be campaign victory points.** The server's rank weighting and capture credit
must not be reconstructed by simply reusing one of these values.

At `0x501042`, the client passes an area handle, a game identifier, a per-player
identifier, the property object and a final-report flag to wrapper `0x5ab719`.
That wrapper calls interface slot `0x78` and returns a boolean. The call site
then releases the temporary property object. The final flag is set by comparison
with local event value 7 (`0x500f6e`); the meaning of every event value and the
full gating logic remain untraced.

The interface is provably Rover: `0x5ab08e` loads Rover and resolves
`_GetInterface@4`, storing its result at `0x65dec4`. ROVER's DLL initialization
calls `0x10001000`; that initializer assigns interface slot `0x78` to
`0x10004132`. The table is initialized at runtime, not stored as a complete
on-disk pointer array.

ROVER `0x10004132` calls builder `0x1001426c`, which forwards to `0x10014de0`.
That builder prepares these named properties:

| Property | Observed construction |
|---|---|
| command | `score_report` |
| area_id | Area argument |
| game_id | Game identifier argument |
| dpid | Player identifier argument |
| last | Added with value 1 when the final flag is true |

The wrapper merges the supplied score properties into the constructed object
(`0x10004160`–`0x1000416f`) and passes it to `0x1000df32`. These are recovered
message-object fields, **not yet a verified wire packet**. Serialization,
transport framing, acknowledgement/retry handling and server validation remain
UNKNOWN. No original service was contacted.

## Separate optional reporting interface

EXE-3 also loads `./kreporter` at `0x4ffeef` and resolves `_RIReport@44` into
`0x640578`, called at `0x5010d4`. This path is distinct from the Rover call above.
No matching loose reporter file was found in the two extracted patches or the
surveyed local game trees. Its absence does not block the observed Rover trace
and does not prove that Boneyards reporting depended on it.

## Remaining boundary work

The [transport follow-up](report-transport.md) traces `0x1000df32` through
queueing, serialization and socket submission. Next, identify acknowledgements and
finalization, and determine the relationship between reporting events and
surrender/disconnect outcomes. The client boundary alone cannot establish how
the historical server deduplicated reports, weighted ranks, or credited orphan
battles. Territory graph and fatigue arithmetic remain separate open questions.
