# .crt trigger section — SOLVED

Follows the placement records (`12 + count*568`):

```
header   { i32 version = 9; i32 numTriggers; i32 ?; i32 ? }   // 16 bytes
records  { i32 params[1..3]; char slots[5][64] }              // repeated
trailer  { i32 numDefs;
           numDefs x { char name[64]; u8 uninitialized[192];
                       i32 x1, z1, x2, z2 } }                 // 272 B each
```

- Slot strings are operands: unit type names (`ARASWORD`), region names
  (`The Hill`), player names (`Player 1`), and ASCII numeric literals
  (`600` = seconds, `10000`, comparator digits `0/1/3/9`).
- Trailer defs are REGIONS in 16px cells — including per-player start
  zones (`Player N`). The 192 uninitialized bytes are editor heap
  garbage (which produced the false "binary strings" earlier).
- The trailer is anchored at EOF: `4 + numDefs*272` bytes, with the
  count immediately before the first def. Verified byte-exact on KotH.
- KotH program decodes to: timer 600s; score = count of ARASWORD per
  player inside "The Hill"; per-player comparison/action groups.
- Int param semantics (2,9 / 10 / 2,1 / 13 / 1,2,4 / 16 / 2,7...) are
  still opaque (opcode/condition ids); the engine executor extracts the
  {unit, region, time} rule pattern instead of interpreting them.

Implementation: `crt::loadTriggers` (regions + token stream);
`takview game --scenario` derives the victory rule from it, shows a
countdown, and judges the winner (see `--hilltest` for a fast check).
