# TAK Engine 0.9.15

0.9.15 follows 0.9.14. Selection rings stay rings at every zoom, and arrows,
bolts, harpoons and spears draw at their own size. Both undo additions of ours
that the original never had.

**Compatibility.** Both changes are display only. 0.9.15 keeps network protocol
**308** and replay format **15**, so **0.9.15 and 0.9.14 play together in either
direction, and each plays the other's replays.** A server needs no update. Older
versions stay refused: 0.9.13 and earlier do not play with 0.9.15, and their
replays are refused.

## Selection rings

Zoomed far out, a unit whose ring measured under 6 pixels across its radius got a
small filled box instead of a ring. That was our own shortcut for very large
selections; the original has no zoom-out and never needed one. 0.9.14 sized rings
from the unit's ground plate, which is smaller on many units, so the box became
common, more so on a screen with fewer pixels per point. Every selected unit now
draws its two dashed rings at every zoom.

## Arrows, bolts, harpoons and spears

Thirteen projectile models were enlarged at most zooms, up to 1.5 times longer and
3.4 times wider once zoomed out to 1.33 or further, so a Hunter's spear looked too
big. They now draw at the size of their models. The shot's path and what it hits
never depended on this.

## Known limits

- Zoomed out, these shots can be thin streaks: that is what the enlargement was
  added to avoid.
- Zoomed far out, a ring's dashes can be tiny and close together, and a very large
  selection draws 24 small shapes per unit instead of one. The cost was not
  measured.
- 0.9.14's hardware cursor change has still not had a recorded check on a real
  display.
- All of 0.9.14's limits carry over (see
  [release-0.9.14-notes.md](release-0.9.14-notes.md#known-limits)).

Validation: [release-0.9.15-validation.md](release-0.9.15-validation.md).
