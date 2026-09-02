# .crt trigger section — format solved, opcodes partially mapped

Layout after placements (`12 + count*568`):

```
header  { i32 version = 9; i32 numTriggers }                   // 8 bytes
records { i32 params[0..4]; char slots[5][64] }                // repeated
trailer { i32 numDefs; numDefs x { char name[64];
          u8 uninitialized[192]; i32 x1, z1, x2, z2 } }        // regions, cells
```

A record's OPCODE is the LAST int before its slots; earlier ints are
trailing parameters of the previous record. Slots hold operands: unit
type names, region names, "Player N", variable letters (a..o), numeric
literals as ASCII, and message text.

## Opcode map (evidence-based; confidence noted)

| op | operands              | meaning                                   | conf |
|----|-----------------------|-------------------------------------------|------|
| 1  | [seconds]             | WHEN elapsed >= N (sets time context)     | high |
| 2  | [var, value]          | SET variable                              | high |
| 3  | [value, var]          | IF variable == value                      | med  |
| 7  | [type, region]        | SPAWN unit at region (owner = Player-N    | high |
|    |                       | zone when region is one)                  |      |
| 9  | [type, region]        | order/rampage spawned units?              | low  |
| 10 | [type, value, region] | award/score bonus (KotH flags, 10000)     | low  |
| 13 | [type, region]        | SCORE = count of type in region           | high |
| 16 | [n, type, region]     | IF count(type, region) < n (guards        | high |
|    |                       | respawn blocks; also per-player checks)   |      |
| 17 | [var, value]          | variable op (clear/set-0 variant)         | med  |
| 18 | [var, value]          | variable op (set/add variant)             | med  |
| 21 | ['N']                 | min-players / declare-defeat?             | low  |
| ?  | [Player N, "text"]    | SHOW MESSAGE to player at time context    | high |
| 5, 6, 12, 24 | []            | structural (trigger end / else / eval)    | low  |

## Verified reconstructions

- **King of the Hill**: score = ARASWORD in "The Hill" (52,51)-(76,72),
  timer 600s; per-player `[16][9,ARASWORD,Player N]` + `[7][ARASWORD,
  Player N]` = maintain a stream of up to 9 Swordsmen at each start
  zone. The engine now runs all of it.
- **Savannah Hunt**: score = NPCFARM anywhere, timer 900s; two
  alternating respawn waves keep 40 Field Hands alive across regions
  1a..9b; "5 minutes left..." messages to each player at t=600.
- **Angvir's Maze**: no scoring op — last-alive arena with TARGOD
  (Belial) spawns at region 'belial' at 600/720/1300/1480/2400/2700s.
- **Ground War / Varro**: the standard last-alive prologue
  `[16][1,Any Unit,Anywhere]` (player eliminated when nothing left).

Engine: `crt::loadTriggers` returns records+regions; scenario mode
derives the scoring rule (op 13 + following op 1), timed and
maintain-count spawn rules (op 16/7), and timed messages.
