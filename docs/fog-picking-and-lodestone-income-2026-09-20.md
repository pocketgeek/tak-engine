# Fog picking and lodestone income, September 20, 2026

## Hidden object input

The renderer hid enemies correctly, but selection and order hit tests scanned
all snapshot units and simulation features. Client hit tests now require current
visibility for enemy identities and reclaimable features/corpses. Allied live
units remain available as they are in rendering; embarked units cannot be picked.
No-fog mode remains unrestricted. The visibility test uses the render snapshot.

The same filter covers screen-space selection/hover, contextual right-click,
armed attack/reclaim/repair/guard/load picking, reclaim-box expansion and feature
IDs gathered for clear-then-build. Selected enemies are removed when they leave
sight, so their live HUD details do not remain exposed. Ground movement and
attack-move into fog remain available. No visibility checks were added to the
shared command application or navigation: local fog is not lockstep state.

A controlled Debug client under GDB exercised the real input methods on Ulasem
Arena, with the enemy/feature cell set to unexplored (0), explored but hidden (1),
and visible (2). Results:

| Operation | Unexplored | Fogged | Visible |
|---|---|---|---|
| Unit pick | rejected | rejected | accepted |
| Armed attack click | AttackMove | AttackMove | Attack |
| Contextual right-click | Move | Move | Attack |
| Armed single-feature reclaim | no command | no command | Reclaim |

## Lodestones accepted without productive coverage

`canPlace` previously accepted lodestones within 24 pixels of a deposit without
checking its footprint coverage. The economy correctly uses full sacred-site
coverage. Consequently some accepted placements completed and added storage but
produced **zero** mana. This was a simulation placement mismatch, not omission of
buildings from the HUD's source accounting.

For each of the five base lodestones in both balances, scanning offsets around
Ulasem Arena's deposits found 1,503 accepted positions, of which 1,235 produced no
sacred-site income. Placement now consults the same sacred-site coverage function
as income. The scan now accepts 268 productive positions and no unproductive ones.
Synthetic maps that supply deposit points without sacred-site records retain their
existing placement behavior. Income formulas and pathfinding are unchanged.

`placement_test` checks all five factions in both balances. It verifies incomplete
lodestones add neither income nor storage, then verifies completed ones update
actual stored mana and the PlayerR HUD snapshot. On the selected normal-strength
site, income increases from 10 to 20 and capacity from 5,000 to 6,000. The fallback
bottom-right income label and spectator table now round like the mana-bulb label,
avoiding a one-point loss from truncating a floating-point rolling average.

The fix prevents new nonproductive placements; it does not reposition lodestones
in old saved state. Because placement legality affects shared simulation, network
version is now 173. All targets in Release, Debug, and optimized Debug were rebuilt.

Validation completed: 38 CTests passed in each build (114 total). Native compiler
checks retained golden `8adc4762a852fadd`; ARM checks were unavailable because the
cross-toolchain headers/libraries are absent. Two isolated client/server runs each
reached tick 600 with hash `b879f25ebe4532bf`, without desync. No remote servers were
changed.
