# TAK Engine 0.9.24

0.9.24 follows 0.9.23 the same day. Random maps get plateaus, mountains and towns built
from the original game's terrain pieces; Legion's ranged attackers fill several rows deep;
and units you order to attack walk to a clear shot whatever their stance.

**Compatibility.** 0.9.24 uses network protocol **320** (0.9.23 used 316) and replay
format **15**. **0.9.24 does not play with 0.9.23 or any earlier version, and it does not
play their replays.** A server and all of its players update together: a 0.9.23 server
turns away 0.9.24 players, and a 0.9.24 server turns away 0.9.23 players, with a
*protocol version mismatch* message. The random map generator is now version 10: random
map recipes from earlier versions no longer generate (Maps you saved as `.kmp` files
still play). Games on shipped maps in Retail pathfinding play as in 0.9.23 except where
a unit you ordered to attack has its shots blocked (below).

## Random maps: plateaus, mountains and towns

- **Built from the original pieces.** The generator now assembles the original game's
  cliff, plateau, mountain and town terrain kits, the way the shipped maps are built, in
  every world and on every layout (Mainland, Lakes, Islands where there is inland room,
  and each world's own layout):

  | World | Plateaus | Mountains | Towns |
  |---|---|---|---|
  | Aramon | cliffs | high mountains | cobbled towns: houses, wells, carts, a fountain |
  | Taros | cliffs, hillside ramps | high mountains | towns: hovels, buildings, stocks |
  | Veruna | cliffs | none (the original has no Veruna mountains) | towns: houses, lamps, a fountain |
  | Zhon | jungle cliffs | rock spires | villages: tents, huts, a bonfire (the original has no Zhon towns) |
  | Creon | hillsides | snow peaks | towns: houses, civic buildings |

- **Three new sliders** on the random map panel and in Cartographer's generator dialog:
  **Plateaus**, **Mountains** and **Towns** (**Villages** on Zhon). They are independent:
  most plateaus are plain, and each mountain stands on part of a plateau of its own with
  open ground round it. Some labels are shorter to fit: **Rocks**, **Extra Mana**,
  **Water**, **Hills**. With all three at zero a map is the same as before.
- **Ramps.** Every plateau has one to four ramps, the first facing the nearest start.
  Ramps keep to one family of pieces so their edges join cleanly (an automated check over
  6,150 generated maps finds no mismatched joins). Aramon and Taros plateaus are climbed
  by builders and infantry, as on the original maps.
- **Mana on high ground.** Extra mana rounds can stand on plateau tops, under the usual
  fairness rule: every player gets an equally far spot by walking distance (through the
  ramps), nearer to them than to anyone else. Two-player maps of 24 × 24 and up nearly
  always get some, four-player maps about half the time; with eight players it is rare.
- **Fair as before.** Plateaus, mountains and towns keep clear of the starts, their home
  areas and the reserved army routes, and every start keeps its way to its mana and to
  the others.
- **Version 10.** Random map recipes from 0.9.23 and earlier (generator versions 8 and 9)
  are refused with a message to generate a new map; Cartographer's **Regenerate** says the
  map came from an older generator.

## Battles

- **Legion: ranged attackers fill up to three rows deep.** When more ranged units attack
  a target than fit along the edge of their range, the front rows walk in, still firing,
  so the rows behind come into range too. 100 gunners against one target now have about
  89 in range at the end instead of 47, and deal about twice the damage; 64 in formation
  get all 64 in range. Melee units still go round to reach the target, and small groups
  that already fit are unchanged. Units whose weapons must face their target mostly hold
  their fire while they walk in, but they still kill sooner than before.
- **Units you order to attack walk to a clear shot whatever their stance.** A ground unit
  whose shots keep stopping on a hill, water or a rock walks to a spot with a clear shot
  after three of them. Units on **Defensive**, the stance most units start with, did that
  only when they had picked the target themselves on the move, so a unit you ordered to
  attack walked up to the target and then fired into the hill for good. Units on
  Defensive firing at a target they picked themselves still stand and fire, as in the
  original game.

## Screens

- **Spectators and replay viewers** no longer see a selected builder's build menu.
- **Play the Machine:** with **Spectate (Watch AIs)** on, **Fog of War** shows **Full
  Vision** greyed out; turning Spectate off brings back your own choice, which is never
  overwritten.
- **Options:** under **Data folder**, **Settings and replays** shows the folder that holds
  your `settings.ini` and replays; only the **Change** button opens the folder picker
  (a click on the folder name or the lines under it opened it too).

## Under the hood

- Protocol 320 (317 to 319 were never released on their own).
- `mapgen_seams` (tools/mapgen_seams.cpp) checks generated maps for joins between terrain
  pieces that the original maps never place side by side and that show as colour or tone
  steps; `mapgen_kits` checks every world's kits, reachability and plateau mana.
- The `--mpai` gates and the rest of the checks are in the
  [validation report](release-0.9.24-validation.md).
