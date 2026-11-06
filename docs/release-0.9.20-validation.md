# 0.9.20 release validation

**DRAFT.** Scope, compatibility and the local test results on the cut are written;
every part marked **TODO-LEAD** is filled in by the release lead from the remaining
test, CI, signing and publication runs.

Protocol **312** (0.9.19 used 311), replay format **15** (unchanged), generator version
**9** (unchanged).

Tag `v0.9.20` -> **TODO-LEAD** (the preparation commit); annotated tag object
**TODO-LEAD**. Published **TODO-LEAD** UTC as the Latest release.

## Scope and compatibility

0.9.20 follows 0.9.19 (tag `v0.9.19` -> `0a6f52dc`, validation commit `12302989`) and is
cut from `65807ab4` (`65807ab470aeeab8d09660bacd40eb78f33f5105`, origin/main). Its code
reached main in two fast-forwards: the landing branch `land-0920b` up to `58f759d8`, then
`land-0920c` up to `65807ab4`. Each landing branch is a line of merges of the topic
branches named below plus commits made directly on it. `git log v0.9.19..65807ab4`
holds 241 commits besides merges, 110 of them after `58f759d8`. They are grouped here by
area; each heading names the topic branches that area's work was merged from, and the
commits made directly on `land-0920b` or `land-0920c` (most of the mana, change-of-owner
and campaign work among them) are listed under their area:

**Fixes batch, protocol 312** (`fixes-0920` and its parts `fx-blocked`, `fx-navyyard`,
`fx-fade`, `fx-vermage`, `fx-seadragon`, `fx-mirror`, `fx-holdflyer`, `fx-treedie`,
`fx-landdoor`, `fx-guarddoor`, `fx-heightlift`, `fx-resultsclick`, `fx-yardshore`):

- `6047bf01`, `5c65827d`: a blocked-shot write-off lapses when the shooter or the target
  leaves its spot; protocol 312.
- `e54c9d9b`, `3bf45c52`, `4af4e0c4`, `6de3bb75`, `39f8cfe6`, `9019fcb4`, `ab1aa94b`,
  `a257c2d4`: idle units and guards make way at a Navy Yard's or land factory's door
  (the guard door key per factory on a growing serial), Legion's shore yard door.
- `6bd52fb8`: reclaimed trees and rocks fade out as a white silhouette (display only).
- `6fd6a5a1`: destroyed features play `seqnamedie` before `featuredead` replaces them.
- `81aff884`: a flyer on Hold Position stays at its post.
- `335eb065`, `474a76a5`: terrain lift is retail's absolute `z - (y >> 1)`; overlays
  follow a ship's waterline, clicks on water.
- `e616dacc`: result screen and title sub-panels play their button clicks.
- `ccf4634b`, `b16a3c3d`, `1a126493`: investigated cases pinned by guard tests (Sea
  Dragon at 472 px, the Prismatic Mirror site hold, Kirenna's Attack_Chase approach read;
  Attack_Chase itself is not ported).

**Retail target search** (`fx-tsearch`): `7bf30019`..`432bab4d` (steps 1-7: scan helpers
and oracles, the mission hosts' 4d8370 scan, the patrol/wander/formation/VTOL_Move/melee
scans, Guard_NoMove, attack-move as MoveFight, the stride scan and `findTarget` removed),
`21e33219`, `10c5ba36`, `ec9396bb` (allied sight, every ally's sight and exploration
shared), `bf8e3340`, `e3e5c4f1` (passive neutrals share none), `ea0561e3` (the 7-tick
visible-enemy list), `091bf752`, `cf8bbba0` (Legion's stalled aim cleared once),
`37e60230` (Legion slot search starts at the first ring that can hold a cell; same
cells), `8c168af0`, `b68897c0` (Legion oncoming-traffic rules), `ba673310` (TS312
baselines).

**Clicks, fire at a point and queueing** (`fx-clicks`, `fx-suppress`): `2994544c`,
`eb84de14` (retail's per-unit resolver on release, one picker, cursor-gated armed
clicks), `d7dfd0a8`, `c51a4c0a`, `fb84a071`, `c29e5f37`, `d7bd9a76` (Ctrl front-insert
and Shift/Ctrl toggle-remove, rally-aware), `e9e331bb`, `f3aa4ce0` (fight-move is retail's
MoveFight), `2988140c`, `58aefea2`, `092ee00b` (`Cmd::AttackGround`: SUPPRESS, AIRSTRIKE,
AIRTOGROUNDHOVER, AIRTOGROUND), `fa7fb455`, `8e136a9f` (cursors), `8969e0ef` (Ctrl on a
builder keeps its build).

**Order display** (`fx-orderdisplay`): `6aabacfb`, `0c90dd90`, `6626836f`.

**Audio** (`music-native`): `8ab11b80`, `717fc264`, `66f2cacb`, `1ce3f85d`, `cbad5314`,
`99192d41`: 44.1 kHz mix, music at its native rate and channels, polyphase windowed sinc
resampling, the stream at the mix rate. Client only, never hashed.

**Group checks and veterancy** (`fx-groupcheck`, `fx-vet`, `fx-mpoint`, `fx-misc`):
`1819b70d`, `adadc6ad` (SUPPRESS and attack group checks, firing on the walk back),
`25c78f84`, `c5def405`, `20fe7194` (retail kill credit, levels from experience points),
`f8dd59d9`, `893213cb`, `b14007ce` (weapon switchers fall back on mana), `4d721fa5`
(armed factory, the AI's flyer point attack, m/p/u points at X*16), `afe500d0` (Fedora 45
packages), `52ba6f11` (CI builds the Legion selection's programs).

**Kill credit, flyers, guided shots and campaign** (`gap-killcredit`, `gap-flyers`,
`gap-guided`, `gap-campaign`): `0abd228c` (kill credit, death weapons, mission Kill and
deadly water), `9ff8fcef` (death sight for computer allies), `5413a85a`, `8b73f4ff`,
`7e73588f` (flyer patrol flags and attack group check, the AI's air rows), `ccecf461`,
`a9fbcbc8`, `e10be88b`, `98975433` (guided steering: native float-cosine turn
angle and snap, carried targets), `a1e26798`, `20065bcf`, `fc261a90`, `afb6aa8e`,
`94f92e5d`, `3947f888` (campaign order points, placed units, trigger
regions). Landed on main as `58f759d8` with `97350e3b` (tidy) and `468ef498`,
`6d0bf612` (full-suite fixtures).

**Retail groups** (`fx-groups`): `06006ef6` (only a formation paces; the classic AI's
squads are formations), `6a763c4b` (an output joins its maker's group when finished),
`b6d3d52e`, `189bce08` (every flyer mission's group check, the standbys and
FormationStandby, emulated), `38475d40`, `95f3fbbb`, `abed6721`, `c4de2a2d` (walker
guard/attack and transport missions as group movers, Follow_Ground, ground Patrol),
`4ebc8323`, `a9e61563` (passengers stay in their Retail group), `33cf0c2a` (the computer
player's formation takes the speed cap).

**Mana and change of owner** (`fx-mana`): `beaac2ed` (new units start with an empty pool,
retail 511990), `7f41be55` (the state hash folds each unit's mana pool), `33261bfa`
(change of owner as retail's 514da0; deadly water on carried units; gates start
inactive), `1d9b7864` (a game opens with its starting units' storage: 5000 a player),
`58e331b0` (the commander rule), `20008b96`, `7fcc65a5` (a carrier's passengers die with
it, or are lost when it changes sides), `89f9cd74` (self-destruct timing), `cec8aa8c`
(a finished wall becomes its feature), `41724c1b` (`init_cloaked`), `083b43ed`
(`activatewhenbuilt`), `c6e78703` (auras), `6df481db`, `261ce0e2`, `0cd37ae7` (kill count,
rank mark, multiplayer Monarch name), `529f655e`, `b9a424b5` (`damagemodifier`,
`mogriumbounty`), `0caa4089`, `5aaa27f5`, `03238e41` (Losses, CRT Damage, unconjure
deaths), `801be5ed`, `04af4d44`, `92b09a13` (mission death at retirement, the GET table),
`982ed690`, `b9ffefcd`, `fd2d9f2d`, `58a99ce4` (mission orders, PARK, campaign
objectives), `6d7bc996` (death severity sample), `f2642a98` (status line INACTIVE only
for a unit that can be switched off).

**Water graves** (`fx-sink`): `52f487bb`, `b31c8b1f`, `11c167e1`, `5738809c`,
`2e45ee2f`, `00b1425f`, `c4d5ee54` (graves decided by the ground under the body, sink
to their bed and stay, a reclaimed one stops, hashed only when any exist), `aef5a861`
(`nosealeveltrigger`), `4087db3a` (a death blast never reaches its own corpse).

**Legion formation re-form** (`fx-legform`): `69b31f0a`, `a9618fda`, `be7c3e69`,
`3474a783`, `29ad49be`, `542c0f74` (an idle member out of its slot walks back; the AI's
flagged squads too; a member's own move keeps its slot), `5394d534`, `6227acfa`,
`35b5513a`, `538105c4` (fixtures and LF312 baselines).

**Housekeeping and CI** (`fx-hk`, `fx-ci`): `76cada1c` (GCC 13/14 alloc-size warning,
`NOMINMAX`, UBSan wrap), `241bcce8` (Opus falls back to the GitHub release tarball),
`8985bc3c` (streaming deps' cross build passes `CROSS_PREFIX` to OpenSSL), `094aae88`
(retail-gap tests in four parallel slices in the quick label), `81ce6e74`, `1c61f316`
(tests find the extracted retail files).

**Site decay** (`fx-sitedecay`): `1cbe7c22`, `4a3042de`, `a8527320`, `2910a7ef`,
`954ef2a4`, `5434470b`, `6a6e44b3`, `02418da4`, `c1a1f092` (retail's GetBuilt timer and
decay step emulated; abandoned sites decay at retail's pace and refund; a producer's
output is held while its producer keeps it; a site that changes sides starts its timer
afresh; SD312 baselines).

**Late fixes on `land-0920c`** (after `2910a7ef`, all direct commits):

- `f870701c`: an unfinished site that changes sides has no GetBuilt timer and never
  decays (retail); its new owner can still finish it.
- `7295fe8f`, `90447818`: CHANGELOG (one passengers entry, auras in 0.9.19 terms,
  captured sites) and a comment tidy at `creditKillBounty`.
- `11ae8cfa`: `retailFlightFormation` has no variable named `far`, which MinGW's
  `windows.h` defines away (the Windows cross build failed); the draws and their order
  are unchanged.
- `4079e70c`: a finished factory switches on only once its `Create` script has returned
  (ours, not retail; `World::switchScript`, `cob::createHold`, `UnitScript::activateHeld`
  hashed only while set). Production given to a factory as it finished could run
  `Activate` while `Create` still slept before `InitState`, so `RequestState` saw "already
  on" and the factory never opened: Navy Yards and land factories alike. With the 5000
  opening this kept the computer player's Creon navy from ever appearing (`naval_ai`).
  New `factory_activate`: the 11 shipped production structures at three builder speeds;
  the old code fails 9 of 33.
- `a3ac3cfe`, `6963f360`: the self-destruct notice shows the unit's real countdown
  (`selfdestructcountdown`, 2 s for every shipped unit) instead of "IN 5..."; client
  only. New `selfdestruct_notice` (debug harness, the real key path).
- `2bf7445e`: `sim_driver_equiv` starts with the real starting mana, and its
  equivalence half, which always skipped, runs as `sim_driver_equiv_data` (Ulasem Arena,
  150 ticks, direct == wire).
- `65807ab4`: `docs/retail-engine.md` records the Activate hold as ours.

Not in 0.9.20 (looked at, not done): porting Attack_Chase, AI production gates, gods
(they stay off).

**Compatibility.** `kNetVersion` is **312**, so 0.9.20 plays with neither 0.9.19 nor any
earlier version, in either direction, nor their replays (311 replays are refused). The
wire gains two commands, `Cmd::AttackGround` (a player's fire at a point, `092ee00b`) and
`Cmd::AiGroup` (the server's classic AI tells the sims its squads; never accepted from a
client, `06006ef6`), both from `kAttackGroundProtocol` 312; and an order verb's `queue`
byte gains `kQueueFront` (2, Ctrl) and the `kQueueToggle` bit (4) (`d7dfd0a8`). No other
command or message layout changes. Replay format
stays **15** and the random map generator stays version **9**
(`src/net/replayhdr.h`, `src/tnt` and `docs/random-map-generation.md` are unchanged since
`v0.9.19`). Both pathfinding modes play differently: every `--mpai` gate hash changes
from 0.9.19's.

## Full test run

Run against `65807ab4` (origin/main, the code of the release) and its parent line
`11ae8cfa` on 2026-11-06; logs in `~/tak-tmp/t/lv/` (on `11ae8cfa`) and
`~/tak-tmp/t/navy/` (on `65807ab4`). The five commits after `11ae8cfa` are the factory
Activate hold, the self-destruct notice, `sim_driver_equiv` and docs; every check below
that ran on `11ae8cfa` was repeated on `65807ab4` unless stated.

- **Full local suite** (Debug `-O2 -g`, GCC, every label, `-j12`): 904 of 904 passed on
  `65807ab4`, no flake, no compiler warning. On `11ae8cfa` 899 of 901 had passed:
  `naval_ai` failed 3 of 3 (the Creon opening's Navy Yard never opened, bisected to
  `1d9b7864`, the 5000 opening; fixed by `4079e70c`), and `harness_seat_load` failed
  once under load ("can't keep up") and passed 3 of 3 alone. The nine `classic_ai_*`
  tests pass with the 5000 opening, `duels` and `waves` included; `factory_activate`
  fails 9 of 33 cases on the code before `4079e70c` and passes 37 of 37 after;
  `selfdestruct_notice` fails on the old notice.
- **Legion scenario gate** (`legion_check.py`, all 161 scenarios plus wanderers-on, gate
  offsets, both modes): `check` and `check --cumulative` PASS (40,781 keys, 0 failed) and
  `validate` PASS, 0 problems; `tools/scenarios/baseline.json` needed no update and no
  `hash@` entry needed retaking. `ctest -L legion`: 411 of 411 (in the full run).
- **Detmath guard** and **cross-compiler golden** (`tools/check-detmath.sh`,
  `tools/check-determinism.sh`): OK; "DETERMINISM OK", golden `dcef618cd2e4d558` for
  g++ and clang++ at -O0, -O2 and -O3, unchanged from 0.9.19. The aarch64 legs are left
  to CI (no local toolchain; owner's decision).
- **`--mpai` Inner Circle, seed 1**, `TAK_SPEED=80`, each gate twice with identical
  results, on `11ae8cfa` and again on `65807ab4` (unchanged: none of these games gives a
  factory production in the ticks right after it finishes, and the Activate hold is
  hashed only while set). Every hash changes from main before the batch (`58f759d8`):

| Gate | main `58f759d8` | 0.9.20 `65807ab4` | Cause |
|---|---|---|---|
| Retail 60 s | `8f386de1dedca1e0`/4 | `beb81788610d2a6c`/4 | hash layout and state: each unit's mana pool hashed, `activatewhenbuilt` default, standing order stored as 2, site decay timer, opening pool |
| Legion 60 s | `54cecac0594ca32c`/4 | `87efa5106c2d3c0e`/4 | same |
| Retail AI 60 s (`TAK_AI_LEVEL=6`) | `675f374bad6cf01b`/4 | `a3ddc1ba59738936`/4 | same |
| Retail 300 s | `8cd6c68861a2f9d0`/24 | `30a3e25d3f414fad`/9 | same, plus the 5000 opening |
| Legion 300 s | `a90b4f8a5a50bd61`/12 | `bd274b29f05f2c18`/9 | same, plus the 5000 opening |
| Crusades, Retail 300 s | `da8094b955669fa2`/26 | `23d5e2016eae1bb8`/10 | same, plus the 5000 opening |
| Crusades, Legion 300 s | `80c0d8f2df1ac2a7`/11 | `ee9c38ff55448ab3`/10 | same, plus the 5000 opening |

  The unit-count drop at 300 s is the starting mana: a probe build of `11ae8cfa` with only
  the Monarch's opening set back to 2800 gives main's 300 s counts again (24, 12, 26, 11).
  0.9.19's gates (`8690eaee7b16b4e6`, `b558575535124afc`, `12003fba552b8696`,
  `269f3da1835a17ea`, `f8931be77508f851`, `6f8d58fdcd304fc7`, `c036ee3dcc43e84e`) all
  changed with protocol 312 before `58f759d8`.
- **Local desync sweeps** (`tools/desync-hunt.sh`, 34 cases, 20 game-minutes each, Debug
  `-O2 -g`): twice on `11ae8cfa` and once on `65807ab4`. No desyncs in any run, every
  run ended cleanly, and the two `11ae8cfa` sweeps' final hashes are identical. 32 cases
  compare real hashes; the 2 benchmark cases play as spectators and compare none.
- **Campaign missions**: all 74 missions 30 s each, on main (`58f759d8`), `11ae8cfa` and
  `65807ab4`: no crash, stall or load failure; on `65807ab4` all 74 outcomes and hashes
  equal `11ae8cfa`'s. Every hash moves from main's, as expected (unit mana and the
  selectable flag hashed, units start inactive, the standing order stored as 2, the
  opening pool from the placed units' storage instead of 500). The large unit-count drops
  are walls turning into features, matched against the walls counted at setup:
  takmission02 player 0 286 -> 16, takmission27 47/105 -> 16/25 (31/80 walls),
  takmission36 85 -> 20 (65), takx07 111 -> 63 (48), takx09 134 -> 62 (72), takx14
  104 -> 72 (32), takmission17 23 -> 16 (9 walls at 30 s on main). Mission 4, with a
  driver that cuts one side to 3 units after 1 s: on main cutting your own army wins at
  once and cutting Veruna's does nothing in 60 s; on 0.9.20 cutting your own does
  nothing and cutting Veruna's wins after 36 ticks (`retailgap_test`'s takmission04
  cases cover both).
- **Release and Windows builds**: a Debug build, a release-config build and the Windows
  (MinGW) cross build of `65807ab4` all build with no compiler warning (the cross build
  failed before `11ae8cfa`'s rename).
- **Release and Clang builds** (Release; Clang 22.1.8 Debug `-O2 -g`), from a detached
  worktree of `65807ab4`, cores 4-15; logs in `~/tak-tmp/scratch/release-0920/local/`:
  both build with no compiler warning. Release `ctest -L quick`: 577 of 577. Clang
  `ctest -L quick`: 611 of 611. Release `takclient --version` and `takserver --version`
  report `0.9.19 (build v0.9.19-275-g65807ab470ae)`: the tested commit, before the
  version bump. (A first try configured without `-DTAK_TEST_DATA` ran only 437 quick
  tests and failed `cli_tools` on its missing `--data` argument; that was the
  configuration, and the rerun with the data passed.)
- **ASan + UBSan** (GCC 16 Debug `-O1 -g -fsanitize=address,undefined
  -fno-omit-frame-pointer`, `ASAN_OPTIONS=detect_leaks=0`, full suite without the nightly
  label, `-j12`): 725 of 725 passed in 56.2 minutes. No AddressSanitizer report appeared,
  and `LastTest.log` holds no UBSan "runtime error". 0.9.19's 8 signed-overflow reports in
  `src/sim/retailflight.h` are gone: `76cada1c` does those far-apart subtractions as
  wrapping uint32 (`retailWrappedSub`), which closes that 0.9.19 follow-up. The build's
  10 warnings are `-Wmaybe-uninitialized` inside libstdc++'s `<regex>`/`std::function`
  headers in one test file (`tools/mod_transfer_test.cpp`), as at 0.9.18 and 0.9.19; no
  engine source warns.
- **Python suites ctest does not run**: all 23 `tools/re/*_test.py` passed
  (`crusades_balance_audit` 5 skipped, `crusades_inventory` 1, `verify_official_hpi` 1,
  as at 0.9.19); `acme_ari` (84 s) and `acme_recovery` (72 s) passed with the Clang
  build's `acme_test_driver` and `tls_test`. `acme_pebble_test.py` was not run (no Pebble
  here).
- **Remote multiplayer sweep** (`tools/desync-hunt-remote.sh --validate --minutes 10`,
  `TAK_WALL_TIMEOUT=3600`). Referees on `tak.pgnet.us` (10 at a time) and
  `vpn3.pgnet.us` (6 at a time) ran a sweep-only `~/takserver.sweep1106`, built in the
  `tak-sweep-builder-0719` Ubuntu 24.04 image (Debug `-O2 -g`, at most GLIBC_2.38,
  SHA-256 `aa6b8d5fc1dadcb77e9ec0265cebbb9ca0302935b5a36ec79c5ce407ad1e04c1`); clients
  were a local Debug `-O2 -g` build of `65807ab4` on cores 16-23. All three `--validate`
  gates passed in both modes ("both at v0.9.19-275-g65807ab470ae", the planted "client 1
  DESYNCED at tick 900" detected, game mods transferred and the wrong loaded gameplay
  rejected).

| Mode | Cases / seats | Ticks | Client-pair checkpoints | Client-referee checkpoints |
|---|---|---|---|---|
| Retail | 37/37, 52/52, "no desyncs reported (52 seats, all completed)" | 650,517 (3 games won) | 12,055 across 22 seat pairs, 0 mismatched | 2,889 across 51 comparisons, 0 mismatched |
| Legion | 37/37, 52/52, "no desyncs" (1 case rerun, below) | 652,350 (2 games won) | 11,826 across 22 seat pairs, 0 mismatched | 2,891 across 51 comparisons, 0 mismatched |

  Wall time: Retail 17 min 9 s, Legion 24 min 57 s, the rerun 11 min 31 s. Largest end
  state: Retail 6,316 units (`h-stress`), Legion 5,174 (`h-everything`). In the first
  pass, Legion `2h-stress` (Ulasem Arena, `TAK_STRESS`, two humans) ended with "the wait
  ran out: an AI took over your seat": the local host seat, at about 14,200 units, held
  the game 7 times and used up its 30 s wait. The second seat and the referee played on
  to 18,000 ticks and all their checkpoints, the host's ticks before the takeover
  included, agreed (0 mismatched). The cause was local CPU starvation, as at 0.9.19 (8
  client cores, other release work keeping the load at about 16-40). Rerun alone
  (`--only 2h-stress`, Legion), both seats ran to 18,000 ticks with no desync, no "held
  the game" line and 0 mismatched checkpoints (final hash `9b62229f5146d36e`). The
  Legion row counts the 36 first-pass cases plus the rerun one. Retail (1) and Legion
  (9) `2h-orders-stress` also logged "held the game" but completed normally.

  The only DESYNCED line is the planted one in each mode's `validate.server.log`, and the
  only mismatch line is the expected game-mods reject; no REFEREE SUSPECT, assert, crash
  or bind-failure line appears in the 253 logs, and every completed run is `err=none`.
  All 75 referee replay headers (37 Retail, 37 Legion first pass, 1 rerun) read format
  15, protocol 312, engine 0.9.19 (the commit before the version bump) and the right
  mode. All 37 final hashes moved in both modes (0 of 37 equal 0.9.19's), as expected
  from protocol 312 (each unit's mana pool hashed from tick 0, the 5000 opening, retail
  groups, site decay and refunds, sinking graves, owner-change rules and the Legion
  re-form changes); fewer games now conclude before the 18,000-tick limit (Retail 3,
  0.9.19 10; Legion 2, 0.9.19 9), and the stress cases end with fewer units (Retail
  `2h-stress` 7,941 -> 5,751, Legion `w-allai-stress` 3,794 -> 1,193). These are new
  references, not desyncs: every case's seats and referee agree. Everything the sweep
  put on either host (the binary, its replay and cache folders, 53 logs on `tak` and 26
  on `vpn3`) was removed afterwards, with no leftover process or netem. The public
  `takserver` on `tak.pgnet.us` (PID 26041, started 2026-11-03 20:12:58) was the same
  process before (17:33:53Z) and after (18:36:04Z) and was never contacted.
- **Mac and Windows**: no separate `--mpai` builds on the MacBook or oden-win this cut
  (neither the sim build flags, the compilers nor CI's determinism coverage changed since
  0.9.19); macOS CI (Apple Silicon) and Windows CI (MinGW, the shipped toolchain) run the
  detmath golden, `sim_driver_equiv`, `navigation_determinism` and the rest of their
  suites (see CI and packages). The packages' signing was checked on both hosts (see
  Signing).

## On the release commit

On `f485e33f` (the preparation commit before these test results were added to it: the
same code and version line as the tagged commit), on Linux: a fresh Release
build (all 1,107 targets) built with no compiler warning, and `takclient --version` and
`takserver --version` report `0.9.20 (build v0.9.19-276-gf485e33f7184)` from scratch
profile dirs, leaving no files. The preparation commit changes only the version line and
docs, so the suites, gates and sweeps above, run on `65807ab4`, stand for it; tag CI
builds and tests the tagged commit itself.

## CI and packages

**TODO-LEAD**: main and tag CI run ids (Determinism, Linux packages, Windows, macOS),
reruns, asset count and names, digests, build ids, NEEDED and `TAK_` checks, the Fedora
RPM server game.

## Signing

**TODO-LEAD**: Windows Authenticode, macOS notarization and stapling.

## Publication

**TODO-LEAD**: draft, tag, publish time, links checked.

## Limits

**TODO-LEAD**: anything accepted at the cut. Known in advance: Kirenna on a Retail
attack-move still often stops after one shot (the original game's own race); a ship
leaving a shore-facing Navy Yard door sails out slowly (as in the original game); a
modded `Create` script that can return but in practice never does (say, waiting on a
called script that never ends) would hold its unit's Activate for good (no shipped
script does; a `Create` with no reachable return holds only until its build-wait ends).
