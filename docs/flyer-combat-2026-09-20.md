# Flyers refusing combat after landing

The user's Crusades replay `game-1-1789958261.takrep` reproduces the failure on
Ulasem Arena. A fresh simulation under protocol 173 matched the recorded hashes.
Move Fight orders for drakes 46/49/51/53/56 arrived at tick 24,805; another group
was sent at 28,094, followed by explicit attacks at 29,799. The affected drakes
were alive, Fire At Will, and near enemies, but had `active=false` and never fired.

The retail VTOL landing callback clears `active`. Both `findTarget` and
`tickCombat` treated that as a universal power-off flag, so landing permanently
silenced ordinary flyers. Those combat gates now interpret inactivity as power-off
only for types that actually support an on/off switch. The landing controller and
its state transitions are preserved.

The requested all-flyer check exposed two other combat integration errors:

* The movement hold check used the longest weapon range even for weapon-switching
  units. Several dragons/priests stopped outside their selected primary weapon's
  range because another spell reached farther. Holding now uses the selected
  range, matching `tickCombat`; independent multiweapon units retain maximum range.
* Stationary flyers aimed with `turninplacerate`. Several flying types omit that
  ground-unit field, making them incapable of turning toward nearby targets. Flyer
  combat now uses `turnrate`, as their flight navigator does.

`flyer_combat_test` loads all 19 armed flying types without an on/off switch from the current
install, including campaign/alternate types, in both standard and Crusades balance.
Every fixture moves and lands through the real controller before introducing a
stationary, harmless enemy. It checks automatic acquisition, Move Fight, and direct
attack from outside range. Successful firing is observed through `justFired`, so
mind control and delayed-effect weapons count correctly instead of requiring
immediate HP loss. The authored Crusades Angel's eight-pixel maneuver leash is
respected in the idle-acquisition case; explicit attacks and Move Fight still fire.
A switchable control confirms power-off still prevents firing and power-on restores
it. This is 114 flyer/order/balance cases plus the switch controls and landing checks.

Re-simulating the user's recorded commands with the fix makes the first drake group
fire repeatedly and defeat the enemy. This diagnostic deliberately accepts the old
protocol only in a temporary loader; changed combat naturally diverges from the old
hash trail. The production replay loader still refuses incompatible recordings.

Network protocol is 174. This patch changes combat integration, not the ground
pathfinding or the retail flight movement/landing routines. All targets must be
rebuilt together in Release, Debug, and optimized Debug.

Final validation: all 39 CTests passed in each build (117 total), including the
new data-backed flyer test. Native compiler/optimization determinism retained
`8adc4762a852fadd`; ARM cross-checks were unavailable due to missing target headers
and libraries. Two isolated multiplayer runs reached tick 600 with matching hash
`b879f25ebe4532bf` and no desync. All local targets are rebuilt; remote servers were
not modified.
