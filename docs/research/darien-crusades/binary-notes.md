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
