# Allied unit gifting

Press **D** during a skirmish or multiplayer match to open **Diplomacy**.
Every participating player appears, including defeated players; empty slots do not.
Each row has **Share Mana**, **Chat**, and **Give Selected Units**. Gifts and mana
sharing are available only for living teammates, not yourself or opponents.
The gift button additionally requires at least one eligible selected unit.
Clicking it leaves the window open and removes submitted units from the selection,
so exhausted selections immediately disable the gift buttons. D, Escape, or Close
closes the window. The game keeps running.

Share Mana controls **outgoing surplus** to that teammate. It defaults on for
teammates and is sequenced through lockstep. Surplus still goes first to permitted
recipients with the lowest storage fill percentage; disabling outgoing sharing
does not stop incoming gifts of mana. Chat defaults on for every player and chooses
who **receives your messages**. The server filters delivery. Default broadcast
includes spectators; directed messages do not. Lobby chat remains room-wide.
Preferences reset for a new match; mana preferences replay through the command log.
Allies always share vision. Campaigns, replays, and spectators cannot use this menu.
Guard stays **G**, and selecting magic units stays **Ctrl+G**. The diplomacy
binding can be changed in Controls.

The simulation checks ownership, alliance, living players, recipient population
and `totalallowed` limits when each gift executes. It rejects monarchs, unfinished
construction, dead/dying units, airborne units, embarked passengers and loaded
transports. Empty, landed transports and completed buildings are eligible.
A mixed selection transfers only eligible units, in selection order; it never
exceeds the recipient's limits.

Transfers preserve health, experience, personal mana and location. They clear
orders, construction queues, repeat production, rally orders, self-destruct and
control-group/formation membership. They move live population counts without
awarding production, loss or kill score. Submitted units leave the current selection immediately; the referee still
rechecks ownership and recipient limits when the command executes. Render geometry keys include owner and color slot, so both
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

The simplified diplomacy menu and recipient-cap checks are engine policy. The
unfinished-site restriction avoids transferring an active construction contract.
No retail executable or assets are distributed with these notes.

`unit_gift_test` exercises the command/wire path, eligibility, cleanup, counts,
score preservation, duplicate commands and recipient caps. It also covers mana
overflow priority: lowest storage percentage first, equal percentages sharing
proportionally to capacity.


## Protocol and validation (2026-09-29)

Version 0.7.12 uses protocol **195**. Both clients and server must be rebuilt;
0.7.11 release binaries use protocol 194 and cannot join these matches.

The dialogue input regression opens/closes D, checks a sparse player roster,
exercises default and changed checkboxes, and verifies mixed selections, disabled
opponent/empty-selection controls, and immediate post-gift selection/group cleanup.
The economy regression covers directed sharing, need priority, re-enabling,
no recipients, incoming sharing, rejected targets, wire encoding, and hashing.

A live host/peer/referee/late-spectator test applied and restored sharing preferences
through lockstep, including a deliberately forged command sender that the server
corrected. All clients matched through 300 ticks. Chat tests verified the intended
recipient, excluded players and spectators, an empty recipient mask, and default
broadcast. Run the existing network fixture with `TAK_DIPLOMACY_NETWORK=1` for the
allied-player plus enemy-AI variant.
