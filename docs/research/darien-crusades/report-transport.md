# Report queue, encoding and incoming reports

This extends [the battle contract](battle-contract.md). All addresses refer to
ROVER, SHA-256 `ffedddf9b615e7a60303231c98541ab0147d55fd8d2e1de510272dff0481ffd6`.
Classification is RECONSTRUCTED from static code, with the narrow native checks
listed below. No original server was contacted and no game was launched.

## Local enqueue is not server acceptance

The `score_report` interface returns true after `0x1000df32` enqueues a message.
That routine enters a critical section, appends a message pointer to a linked
queue, signals an event when the queue becomes nonempty, and leaves the critical
section. Its counterpart `0x1000dee5` removes a queued pointer and resets the
event when the queue becomes empty.

The worker at `0x10011839` handles queue events 6/7/8, selecting queue offsets
0/0x3c/0x78 and corresponding connection objects. `0x100118a9` calls the selected
connection's send routine `0x10010c5c`. The score-report interface selects one
of the first two queues based on a connection-state flag. Its returned boolean
is therefore **local construction/enqueue success**, not a campaign-credit or
server-acknowledgement result.

## Message representation

The score builder requests section type 6. The initialized name table maps that
type to `GameManager` (`0x10015ca3`, accessor `0x10015cec`). The serializer at
`0x10010fc6` emits an outer brace pair around sections. A rebuilt section at
`0x10012df7` emits square brackets containing its name and properties. A rebuilt
property at `0x10013be5` emits an angle-bracket record with type marker, colon,
key, equals sign and encoded value. The overall structure is thus:

```text
{[section-name<type:key=value>... ]...}
```

The space in this schematic is for readability, not a recovered required byte.
Cached serialized sections/properties can bypass rebuilding. This is the same
family of tagged representation observed in Darien.def; it is not JSON or TDF.
This pass does not establish canonical property ordering or every valid type.

Value escaping at `0x10013efe` replaces each of the delimiter characters
`{ } [ ] < > : = %` with percent followed by its zero-based decimal index in
that ordered set. Thus literal percent becomes `%8`, and an equals sign becomes
`%7`. This is **not URL percent-hex encoding**. Encoder `0x10013eb9` and decoder
`0x10013ee2` implement the mapping. This evidence concerns value encoding, not a
claim that arbitrary delimiters are supported inside keys or section names.

## Socket submission

`0x10010c5c` serializes into a 1024-byte-capacity buffer, records the produced
length and applies `0x10010e8b` before calling the Winsock send import through
`0x100160a2`. The terminator written after serialization is outside the recorded
length passed to send. The routine advances its pending pointer and reduces its
pending byte count on partial writes.

The transform XORs each byte with the low eight bits of a connection counter,
then increments the 32-bit counter. The connection setup path initializes send
and receive counters to one (`0x10010c33`–`0x10010c3c`). The inverse receive path
uses the separate receive counter. This is a reversible byte mask, not evidence
of cryptographic authentication or report integrity.

The send routine returns local status 1 after finishing the buffered send,
status 2 for Winsock error 10035, and status 0 on several failure paths. The
worker handles status 2 separately. A complete retry/reconnect audit is still
needed: these local statuses are not server acknowledgements, and successful
partial writes alone do not prove exactly-once reporting.

## Incoming battle reports

Two incoming handlers compare the command with `battle_report`, at
`0x1000a378` and `0x1000a522`. They require area_id, game_id, user_id and username
properties and check the current area/game before dispatching callbacks.

The first handles named legacy statistics such as kills, losses, energy, metal
and score. The second iterates additional typed properties while excluding
routing/identity fields. These are incoming report notifications. This pass
has **not** shown that they acknowledge this client's submitted `score_report`,
that they carry Crusades capture credit, or that receipt means a final result
was durably committed. Do not equate a matching game ID with an acknowledgement.

## Native validation

On 2026-09-30, the original small codec routines were emulated with Unicorn
2.1.4, using the fingerprinted DLL mapped at its preferred image base and
synthetic buffers. No network calls or DLL entry point were executed.

- All nine delimiter mappings and their inverse mappings passed.
- An ordinary non-delimiter encoder input and an out-of-range decoder input
  returned the expected invalid marker.
- XOR transformation matched the reconstructed operation for seeds 1, 254 and
  0xffffffff over 512 bytes, including low-byte and full-counter wrap.
- Splitting the same buffers into 13, 243 and 256 byte calls produced identical
  output to a single call, with the connection counter carried forward.

These checks validate only the isolated codec operations. They do not validate
end-to-end delivery, escaping of an entire parsed message, server policy or
retry behavior. The local harness was scratch work; no original code or assets
are committed.
