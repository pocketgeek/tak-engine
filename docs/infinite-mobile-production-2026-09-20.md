# Infinite mobile production — 2026-09-20

Mobile producers with `repeatType` set accept only Stop through the shared
simulation command dispatcher. This covers player, network, replay and AI
commands and runs before redirect/cancel-build handling. Rejected commands do
not alter the current site, production progress, queue or movement controller.
Shift-queued commands, additional production, unqueue, repeat toggles and stance
commands are also rejected: Stop is the explicit way out.

The lock follows infinite-production state, not queue length or available mana,
so output stalls and between-item gaps cannot unlock it. Starting infinite
production clears an earlier movement/attack order. Autonomous target acquisition
and standby do not take over in an empty-queue gap. Construction's own movement
and hover controller remain available.

Stop clears the repeat state and queue. A Move later in the same command batch
is accepted immediately. Finite mobile production and stationary factory rally
orders retain their previous behavior. Movement/pathfinding algorithms are
unchanged. Repeat state now participates in the lockstep hash; protocol 176
identifies the behavior change.

## Validation

`conjure_test` covers 43 mobile-builder/balance combinations from the installed
roster. It checks every command kind (including queued variants), lock persistence
with an empty queue/no mana, ownership, Stop followed immediately by Move, finite
production, stationary factory rallies, and the repeat-state hash. Existing real
Zhon conjure cases now also reject redirection while the production site and
hover controller are active, and verify Stop clears the controller.

All targets rebuilt in Release, Debug and optimized Debug. All 39 CTests passed
in each build (117 total). Native GCC/Clang fixed-math checks agree on golden
8adc4762a852fadd; ARM cross-toolchain headers remain unavailable. Two repeated
local multiplayer runs reached tick 600 with hash b879f25ebe4532bf and no errors.

Logs: `/tmp/infinite-lock-test.log`, `/tmp/infinite-lock-*-build.log`,
`/tmp/infinite-lock-determinism.log`, `/tmp/infinite-lock-*-tests.log`,
`/tmp/infinite-lock-network.log`.
