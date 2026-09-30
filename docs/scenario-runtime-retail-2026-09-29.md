# Authored scenario runtime: retail evidence

This note records static inspection and targeted Unicorn execution of the local
`assets/game/KINGDOMS.icd` on 2026-09-29. No retail GUI was launched. Addresses
identify observed routines in that executable; no game code or assets are
included here. This is evidence for the authored-scenario work, not a claim that
every CRT action, neutral-player lifecycle, or retail campaign is reproduced.

## Rule ownership and loading

CRT rule sets have a different indexing convention from placed-unit owners.
The nine serialized rule sets are **All Players**, then **Player 1 through
Player 8**. Placed-unit owner zero still identifies Player 1.

The per-player rule loader at `0x4ccb20` increments its authored-player argument
at `0x4ccbcb`. It traverses the serialized sets in ascending order and accepts a
set when its index is zero or equals that incremented argument
(`0x4ccd0b`–`0x4ccd21`). Thus All Players rules execute before the player's own
rules. It compiles separate rule groups into each runtime player's state through
`0x4c9e80`; a group's enabled state is not shared across those player copies.

This corrects the previous engine interpretation of the rule vector as
zero-based player ownership. The ninth rule set is Player 8, not a neutral rule
set. The setup loop calling the loader (`0x4cd4f3`–`0x4cd55e`) filters occupied
runtime player records; this inspection does **not** establish how retail
registers a separate neutral/nature owner. Do not infer neutral rule ownership
from the CRT's nine rule sets.

## Explicit victory and defeat

The action dispatcher at `0x4cad50` uses the jump table at `0x4cbc14`.

| CRT action | Observed recipient behavior | Branch |
|---|---|---|
| 5, Victory for me | Win for the rule's owner only | `0x4cae72` |
| 6, Defeat for me | Defeat for the rule's owner only | `0x4cb844` |
| 21, Victory for me and teammates | Win for qualifying allies, then owner | `0x4cb68d` |
| 22, Defeat for me and teammates | Defeat for qualifying allies, then owner | `0x4cb771` |
| 23, Victory for opponents | Win for qualifying nonallied players other than owner | `0x4cb855` |
| 24, Defeat for opponents | Defeat for qualifying nonallied players other than owner | `0x4cb922` |

The ally branches test both relationship bytes, not merely one directional
relationship. Recipient loops filter valid occupied player records. Our fixed
lobby teams supply the corresponding mutual-alliance relation.

A direct Unicorn call to the dispatcher, substituting only its result-delivery
and network-context helpers, used three occupied players: owner 0 allied to 1,
and opponent 2. Recorded calls were respectively `win(0)`, `lose(0)`,
`win(1), win(0)`, `lose(1), lose(0)`, `win(2)`, and `lose(2)`. All six calls
returned normally. This observes native recipient selection without launching a
match or substituting the dispatcher itself.

Victory helper `0x4f6b90` sets the addressed local player's result bit and result
deadline. It does not defeat that player's opponents. Defeat helper `0x4f6be0`
sets the local loss result and, for the scenario runtime, marks the addressed
player defeated. Both local-result branches require the existing deadline to be
negative, so the first terminal result owns the displayed result. The scenario
player-defeated write at `0x4f6c59` is outside that deadline guard: a later defeat
still marks gameplay defeat even after victory was displayed. That write is
immediate, so subsequent players' rules can observe it in the same evaluation.
The scheduler checks the defeated flag before each player's complete rule list
(`0x4cd02b`), not between that player's groups. The accompanying cleanup helper
`0x5131b0` visits the player's active unit slots, sends death type 10 through
`0x512610`, then retires still-active units through `0x512ae0`. The engine uses
its existing death and retirement paths for immediate removal, including
construction sites, without a self-destruct countdown. This does not establish
every post-result multiplayer behavior.

Consequently, implementing victory by forcing all opponents to lose conflates
independent retail actions, cannot represent a single-player victory, and is not
the observed behavior. Deterministic per-player terminal results are appropriate
for this engine's lockstep architecture; a single campaign-result broadcast is
not a substitute for individual authored-scenario outcomes.

## Standard elimination is bypassed by the scenario runtime

The CRT runner's vtable at `0x5f26a4` has an unconditional true-returning function
at slot `+0x20` (`0x531f00`). The normal result-update path checks that virtual
function at `0x4f6e03`–`0x4f6e10` and bypasses standard victory/defeat-condition
checks when it returns true. The ordinary condition evaluators otherwise called
there are `0x5233a0` and `0x523410`.

This behavior follows having the CRT scenario runner, rather than scanning for
whether its rules contain a terminal action. The runner constructor is
`0x4cc5d0`; selection in `0x4d2750` checks scenario metadata and the scenario file
before installing it. An explicitly authored scenario with no terminal rule can
therefore remain a sandbox instead of automatically ending when a monarch or all
units disappear. Ordinary skirmishes and campaign mission setup remain separate
engine modes.

## Scheduling and repeated conditions

The native scheduler at `0x4ccf90` evaluates when the integer game second advances
(`tick / 30`), or during the explicit initial evaluation. Setup raises its
initial flag and calls it immediately at `0x4cd567`–`0x4cd575`. The start-of-game
condition reads each runtime player's initial-pass flag; that flag is cleared
after the player's rules run.

Before checking rules, existing timers advance or decrease by one. Countdown
timers are signed and can become negative; they are not clamped to zero. A timer
created by an action is therefore first advanced at the next evaluation.

The group evaluator at `0x4ca560` checks the enabled byte, evaluates all
conditions, then executes all actions when they hold. There is no false-to-true
latch in this path. An empty condition list passes. A persistent true condition
runs again at the next evaluation unless its actions disable the rule or change
the condition.

A targeted Unicorn scheduler check installed one enabled group, substituted its
condition as always true, and recorded action calls plus a preexisting countdown
timer. Calls at ticks 0, 1, 29, 30, 31, 59, 60 and 90 produced actions only on
the explicit initial pass at 0 and at 30, 60 and 90. With the timer initially 1,
recorded values were 0, -1, -2 and -3. Only the initial evaluation reported the
start-of-game flag. This independently confirms the scheduler's second cadence,
repeated-true execution, and unclamped timer behavior.

The earlier documentation describing the engine's false-to-true latch as retail
behavior was incorrect. Other condition/action details require their own
evidence; this check does not validate all 26 condition and 26 action opcodes.

## Custom types and placed-unit statistics

The custom-type application routine `0x4cd590` initializes the unit's attack and
armor factors to 1, looks up the type record, then reads armor at record `+0x26`
and weapon at `+0x2a`, multiplying each by the stored single-precision 0.01.
It passes `+0x2e` to the veteran setter `0x519370`. That setter initializes the
experience accumulator and respects the type's `noveteran` flag. The record's
health field at `+0x22` is not consumed here; review of the CRT vector's other
consumers found name resolution and loading, but no health application.

Placement processing `0x4cd32d`–`0x4cd3e9` replaces the veteran default, multiplies
the existing armor/weapon factors by placement percentages, and sets starting HP.
Health is current HP times the percentage and stored float 0.01, rounded to a
float before integer truncation, clamped between zero and the type maximum.
It does not modify maximum HP. The lower-bound constant at `0x5eb524` is 0.0.

`python3 tools/re/check_scenario_stats.py` observes 33 cases against the installed
binary: 20 custom-default combinations (including varying unused health), three
combined type/placement armor and weapon products, and ten placement-health
cases. The product and health checks execute the native instruction ranges;
health conversion uses retail's own float-to-integer routine. The custom-default
probe substitutes the veteran setter only to observe its arguments, rather than
claiming that those cases test the entire experience system. No retail GUI is used.

Neutral placement support is an engine policy: separate nonparticipant ownership,
no automatic hostility, explicit eligible attacks/capture, and a neutral display
color. The CRT rule-set count does not establish retail neutral-unit behavior;
this work does not claim a complete native neutral lifecycle comparison.

## Engine validation

All targets were rebuilt in Release, optimized Debug and AddressSanitizer builds.
The completed CTest runs passed **119/119**, **123/123** and **119/119** respectively
(361 checks). This includes ordinary skirmish, campaign, navigation, combat,
Cartographer UI and the three new scenario suites.

The sanitizer run used `SDL_SHUTDOWN_DBUS_ON_QUIT=1`, dummy SDL video/audio and
`ASAN_OPTIONS=detect_leaks=1:allow_addr2line=1`, with no suppressions. An initial
run without those documented SDL settings reported leaks in 12 SDL tests; the
complete run with the standard cleanup settings passed. This validates the
exercised paths, not every possible lifetime.

Live Release client/server tests used isolated retail-data roots and a map
initially present only on its host:

- `map_transfer_test --solo-network`: one seated player, neutral ownership,
  compounded stats, denied Build/Train/RepeatTrain commands, custom victory and a
  late observer stayed synchronized through 330 ticks, hash `9dbdf35d64b5cf5c`.
- The two-player authored map-transfer test matched host, peer, referee and late
  spectator through 300 ticks, hash `28918a9214d91b6b`. Verified map caches and
  directed-chat recipient checks also passed.
- Release `takclient --play-map` launched its local server, verified the snapshot
  and opened its private lobby with the optional trigger log enabled. The setup
  regression separately verifies that startup rules see default monarchs and
  that their first actions reach the trace sink.

The native stat probe passed 33 cases. The deterministic-math guard passed;
GCC and Clang at O0/O2/O3 agreed on `dcef618cd2e4d558`. Local ARM comparison legs
were skipped because the target headers/libraries are unavailable.
