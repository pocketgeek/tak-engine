# Initial binary trace notes

All addresses below are virtual addresses in **EXE-3**, the official patch's
`KINGDOMS.icd`, image base `0x400000`, SHA-256
`6ddc7fae0cbf3ee13d530614b64608a3aeefca7deb73719310a4f68fe1f22e96`.
Do not apply them to the differently hashed local executable without checking.
These are original static-analysis notes; no retail instructions or data tables
are reproduced. No installer or game was launched for this pass.

| Location | Observed operation | Scope / confidence |
|---|---|---|
| `0x459886` | Supplies callback `0x459a00` and Darien.def to loader `0x5ab7af` | RECONSTRUCTED loader call; full callback semantics pending |
| `0x46b200` | Loads Darien.def with callback `0x46b300`; conditionally opens PreInit.jje | RECONSTRUCTED; object flag at offset `0xc5` gates the table read |
| `0x46b6b7`–`0x46b6e8` | Callback reads chatareaid and fire-anchor coordinates | RECONSTRUCTED using referenced field strings and output locals |
| `0x46b785`–`0x46b875` | Sets initial values in a tree keyed by chatareaid | RECONSTRUCTED; hardcoded ID lists occur here, not copied into this repository |
| `0x4735c0`, `0x4737e0` | Tree helpers compare unsigned numeric keys; smaller keys go left | RECONSTRUCTED ordering for that tree |
| `0x46b244`–`0x46b2d8` | Reads bytes from the minimum node forward using in-order successors; C/H/T set value 0/1/2 | RECONSTRUCTED table consumption; no proof of live-server initial ownership |
| `0x46234b` onward | Crest primary/secondary/shadow sprite setup, followed by wdhit file open at `0x46239e` | RECONSTRUCTED call context, not proven table semantics |
| `0x4623bc`–`0x4623c6` | Decimal scan into integer storage; format reference `0x60b574` | RECONSTRUCTED scanner; supports interpreting JJE as integer data |

Useful next trace targets (string presence only; rules UNKNOWN): territory
support/toughness, momentum, influence, conquest count, battle map scripts,
allowed races, and result handling. Labels such as `darien_id`, `req_honor`,
`req_terror`, `map_races_allowed`, `map_script`, `victory_pnts` and `conq_cnt`
identify places to investigate; their presence is not an implementation spec.

The official `bymaia.dll` (SHA-256
`a4741b677c22b33e29c0226c09a604f9037f3d8252c0eb472c4b31cf72390427`)
exports update-package operations. `Rover.dll` (SHA-256
`ffedddf9b615e7a60303231c98541ab0147d55fd8d2e1de510272dff0481ffd6`)
exports an interface getter and DLL entry point; its authority and protocol
semantics are not established by that alone.

Reproduce with `objdump -d -Mintel --start-address=ADDRESS
--stop-address=ADDRESS /path/to/the/fingerprinted/KINGDOMS.icd`. Keep any
disassembly in local scratch space, not Git. Addresses are research aids, not
new production dependencies.

## Campaign display and entry paths (second pass)

These traces use the same EXE-3 fingerprint above.

| Location | Reconstructed behavior | What it does not establish |
|---|---|---|
| `0x459da0`–`0x459f43` | Loads Borders.png through `0x463fa0`, allocates image-sized storage, and converts indexed pixels through a lookup table to 16-bit values | No adjacency list or attack eligibility graph has been recovered from this path |
| `0x45fdff`, `0x45fe26`, `0x45fe4e` | Fetches `traffic`, `influence`, `victory_pnts` with accessor `0x5aba59`; successful reads store values at territory object offsets `0x1c7`, `0x1cb`, `0x1cf` | Does not reveal their originating server calculation or transport encoding |
| `0x4603ce`–`0x460410` | Fetches the `momentum` string via `0x5abad7`, scans to its terminating zero, counts uppercase H and T, skips other characters | No twenty-entry cap or rolling-history maintenance in this loop |
| `0x460473`, `0x460524` | Publishes the two counts as `hmomentum` and `tmomentum` display fields | Not authoritative result accumulation |
| `0x46054d`–`0x46062f` | If H exceeds T, formats Honor's count over H+T; if T exceeds H, formats Terror's count over H+T; ties bypass both branches | This is display text, not the threshold for capture; no assertion about a preexisting field's value on ties |
| `0x48b070`–`0x48b128` | Battle-entry callback examines its status argument and chooses failure messages, including same-allegiance and allegiance-capacity restrictions | Not the server's implementation of those checks; wire status mapping not fully audited |

The momentum trace independently supports the FAQ's history-based concept, but
the FAQ alone supplies the twenty-battle window. The Borders.png path supports
an image-rendering role; it does not prove the absence of a separate geometry or
adjacency calculation elsewhere. The subsequent [battle-contract trace](battle-contract.md) follows score
properties into Rover's score_report builder. Wire serialization and result
acknowledgement remain untraced.
