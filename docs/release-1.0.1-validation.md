# 1.0.1 release validation

Protocol **324**, replay format **15**, random map generator version **10**.

Tag `v1.0.1` -> TODO (the release preparation commit).

## Scope and compatibility

1.0.1 is 1.0.0 (tag `v1.0.0` -> `5b4bf59e`) plus four changes, listed in the CHANGELOG's
1.0.1 entry:

| Commit | Change | Game state |
|---|---|---|
| `b780a96d` | Construction sparkles of a unit killed mid-build run out instead of freezing on its body | display only |
| `f0ac09e0` | Water graves (wrecks and bodies on a shore or sea bed) decompose as land corpses do; the reclaim hold treats them alike | changed: protocol 324 |
| `21eb6cc3` | A decaying body sinks (1/8 px per update, as retail) and fades over its last 60 updates | display only |
| `ef91dc42` | A ranged weapon that is not a `waterweapon` or `unitsonly` no longer picks, keeps, aims at or is ordered onto a body over water whose SweetSpot is under the surface (a swimming Mer Warrior) | changed: protocol 324 |

Two of them leave retail's behaviour on purpose, by the owner's decision: retail keeps a
water grave for good, and retail goes on shooting at a swimming Mer Warrior, every shot
striking the water short of it (`docs/retail-engine.md`, "Ours, not retail: no shots at
an aim point under water", and the water grave notes).

**Compatibility.** 1.0.1 does not play with 1.0.0 and does not play its replays (protocol
323 -> 324). Games on shipped maps hash exactly as in 1.0.0 in every `--mpai` gate
(below): no unit dies in water or swims in them.

## Local tests

Linux, Debug `-O2 -g` (`build-o2`), on `ef91dc42`:

- **Quick suite** (`ctest -L quick`): 629 / 629. The Release build's quick suite: 448 / 449,
  `cli_tools` failing only because that local build is configured without
  `TAK_TEST_DATA` (an empty `--data`); it passes in `build-o2`.
- **The changes' own tests:** `building_death` and `reclaim` (water graves decay and are
  held like land corpses), `naval_combat` and `legion_naval_combat` (a flagship neither
  targets nor is ordered onto a swimming Mer Warrior, and shoots one standing in the
  shallows; the swimming case fails with the check disabled), `retail_construction`,
  `death_lifecycle`, `factory_death`.
- **`--mpai` Inner Circle, seed 1**, `TAK_SPEED=80`: every gate keeps 1.0.0's hash.

| Gate | 1.0.0 | 1.0.1 |
|---|---|---|
| Retail 60 s | `beb81788610d2a6c` | `beb81788610d2a6c` |
| Legion 60 s | `87efa5106c2d3c0e` | `87efa5106c2d3c0e` |
| Retail AI 60 s (`TAK_AI_LEVEL=6`) | `a3ddc1ba59738936` | `a3ddc1ba59738936` |
| Retail 300 s | `30a3e25d3f414fad` | `30a3e25d3f414fad` |
| Legion 300 s | `bd274b29f05f2c18` | `bd274b29f05f2c18` |
| Crusades Retail 300 s | `23d5e2016eae1bb8` | `23d5e2016eae1bb8` |
| Crusades Legion 300 s | `ee9c38ff55448ab3` | `ee9c38ff55448ab3` |
| Ulasem Arena | `e8b009af2418e33e` | `e8b009af2418e33e` |

- **`tools/check-determinism.sh`**: every build agrees on golden `dcef618cd2e4d558`
  (aarch64 skipped: no cross headers on this machine).
- **The reported game:** the owner's replay of 2026-11-11 20:36, traced headless at its
  fifth minute: a Veruna flagship fired at a swimming Mer Warrior every 3.5 s and every
  arrow struck the sea about 100 px short (aim point 26 below the surface). That
  replay came from a development build of protocol 324 made before the fix, so 1.0.1
  plays it differently from that point on.

## Not run for this release

The full suite on every build, the local and remote desync sweeps and the Windows and
macOS test runs were not repeated: the changes are confined to corpse lifetime, corpse
drawing and one targeting predicate, and the gates above are unchanged.

## Tag CI and packages

TODO
