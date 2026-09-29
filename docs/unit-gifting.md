# Allied unit gifting

Press **Alt+G** during a skirmish or multiplayer match to list living allies. Each
row has **Give Selected Units**. Clicking sends the current selection through
lockstep and closes the menu; Escape or Cancel closes it without sending.
Campaigns, replays and spectators cannot give units. The menu does not pause play.
Vision and automatic mana sharing remain independent of this menu. Guard uses
**G**; selecting magic units uses **Ctrl+G**. All three bindings can be changed
in Controls.

The simulation checks ownership, alliance, living players, recipient population
and `totalallowed` limits when each gift executes. It rejects monarchs, unfinished
construction, dead/dying units, airborne units, embarked passengers and loaded
transports. Empty, landed transports and completed buildings are eligible.
A mixed selection transfers only eligible units, in selection order; it never
exceeds the recipient's limits.

Transfers preserve health, experience, personal mana and location. They clear
orders, construction queues, repeat production, rally orders, self-destruct and
control-group/formation membership. They move live population counts without
awarding production, loss or kill score. Selection cleanup waits for the accepted
ownership change. Render geometry keys include owner and color slot, so both
normal and cached distant models refresh to the recipient's colors.

## Retail evidence

Verified by static analysis of the local `KINGDOMS.icd` and shipped assets:

- `ReadMe.txt` and `gamedata/keys.tdf` bind D to Diplomacy.
- `guis/diplomacy.gui` provides a per-player `ShareUnits` checkbox.
- Confirmation at `0x4b6dff` calls `0x4b6a80` for the checked recipient.
- `0x4b6a80` obtains the selection through `0x520840` and rejects movement mode
  2, either transport attachment pointer, and UnitDef `+0x264` bit `0x40000`.
- The parser at `0x4c08eb` maps that bit to `commander`.
- Accepted units go to ownership transfer at `0x514da0`, including its network
  path. This is a gift, not permission to control another player's units.

The simplified ally-only menu and recipient-cap checks are engine policy. The
unfinished-site restriction avoids transferring an active construction contract.
No retail executable or assets are distributed with these notes.

`unit_gift_test` exercises the command/wire path, eligibility, cleanup, counts,
score preservation, duplicate commands and recipient caps. It also covers mana
overflow priority: lowest storage percentage first, equal percentages sharing
proportionally to capacity.
