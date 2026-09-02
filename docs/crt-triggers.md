# .crt trigger section (UNSOLVED — notes for future work)

After the placement records (`12 + count*568`), a trigger section:

- Header: 4 × u32. KotH: (9, 3, 1, 13). Meaning unknown
  (record/op counts?).
- Records start immediately after; NOT fixed-size. Observed layout:
  one or more 64-byte string fields, then ~200 bytes of parameters.
  - rec0: "ARASWORD" + "The Hill"          (stride to next: 328)
  - rec1: "NPCFLAG"  + "The Hill"          (stride: 324)
  - rec2: "NPCFLAG"  + "10000" + "The Hill" (three string fields; "600"
    appears later — mission length 600s? "10000" = score/interval?)
  - later: "ARASWORD" + "Player 1", ...
- Reading: these encode KotH's rule (count ARASWORD per player inside
  region "The Hill" after the timer; flags ownership via NPCFLAG).
- Next steps: diff trigger sections across maps with simple rules
  (Two Castles, Ground War) to isolate the record framing; the 64-byte
  string slots are likely typed operands (unit type / region name /
  numeric literal as ASCII).
