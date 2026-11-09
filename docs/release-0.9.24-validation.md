# 0.9.24 release validation

Protocol **320** (0.9.23 used 316; 317 to 319 were never released on their own), replay
format **15** (unchanged), generator version **10** (was 9).

Tag `v0.9.24` -> the preparation commit (filled in below once tagged).

## Scope and compatibility

0.9.24 follows 0.9.23 (tag `v0.9.23` -> `0bb9e201`, validation commit `fd8a5cdf`).
`git log v0.9.23..` holds these commits before the preparation commit, all on main
(besides `4c00e7f7`, the in-game picture retakes for 0.9.23):

- `eca625ac`: Play the Machine: SPECTATE shows FOG OF WAR as FULL VISION, greyed (the saved
  choice is untouched).
- `addb8dc8`, `941ee8a1`: Options: SETTINGS AND REPLAYS under DATA FOLDER; only the
  CHANGE button opens the folder picker.
- `665a3c24`, `70141785`: protocol 317, Legion's ranged attack rings fire up to three
  bodies deep and more (`buildRing`'s thick band 0, `walkInRing`); the accepted side
  trades are licensed in `tools/scenarios/baseline.json` (DR317).
- `719646da`: protocol 318, an ordered attacker leaves a blocked firing spot whatever its
  stance (Defensive units ordered to attack had fired into a ridge for good; found in
  the owner's replay).
- `5d90a3b4`: spectators and replay viewers see no build menu.
- `dc284feb`, `e7331ad1`, `ce2e0787`: protocol 319, random maps v10: the original
  game's cliff/plateau, mountain and town kits in every world, Plateaus / Mountains /
  Towns controls, plateau mana under the fairness rules, mountains on part of their own
  plateau; v8/v9 recipes refused.
- `9b47f6dc`: protocol 320, ramps keep one kit family (Aramon cliff ramps, Taros
  hillside runs), found by the new `mapgen_seams` hunt.

**Compatibility.** The protocol changed, so 0.9.24 plays with no earlier version and
plays none of their replays (`replay_test` refuses 316). Random map recipes from 0.9.23
and earlier no longer generate. Games on shipped maps hash as in 0.9.23 in every
`--mpai` gate (below): none of the gate games forms a deep ranged ring or a blocked
ordered attack.

## Local tests on the cut

Linux, `build-o2` (Debug `-O2 -g`), on `9b47f6dc`:

- **Full ctest** (`ctest -j 16`, every label): 909 of 910 passed. The failure,
  `harness_seat_load`, ran out of its script's 60-second budget under the parallel load;
  it passed twice on its own (59.7 s each). It plays a shipped map (Inner Circle) and
  touches none of this release's changes; it has been load-sensitive before.
- **`--mpai` Inner Circle, seed 1**, `TAK_SPEED=80`: all seven gates keep 0.9.23's
  hashes.

| Gate | 0.9.23 | 0.9.24 `9b47f6dc` |
|---|---|---|
| Retail 60 s | `beb81788610d2a6c` | `beb81788610d2a6c` |
| Legion 60 s | `87efa5106c2d3c0e` | `87efa5106c2d3c0e` |
| Retail AI 60 s (`TAK_AI_LEVEL=6`) | `a3ddc1ba59738936` | `a3ddc1ba59738936` |
| Retail 300 s | `30a3e25d3f414fad` | `30a3e25d3f414fad` |
| Legion 300 s | `bd274b29f05f2c18` | `bd274b29f05f2c18` |
| Crusades, Retail 300 s | `23d5e2016eae1bb8` | `23d5e2016eae1bb8` |
| Crusades, Legion 300 s | `ee9c38ff55448ab3` | `ee9c38ff55448ab3` |

- **Legion deep rings** (on their branch, before landing): `ctest -L legion` 414/414; the
  attackring fixtures' reach and damage keys as in the CHANGELOG (mob-100 now_end 47 ->
  89, damage 3,021 -> 5,879; f1-64 44 -> 64); Retail mode hash-identical.
- **Random maps** (on their branch): the map test set 131/131 (map, generator, random,
  Cartographer, `replay_test`, every test with a generated id, the 35 Legion scenarios
  on generated maps, `mapgen_kits`, `mapgen_seams`); a 1,620-map kit sweep and a
  6,150-map seam sweep (every world x layout x 16..64 x 2/4/8 players x five slider sets)
  with no generation failure, no reduced-kit fallback and, after `9b47f6dc`, no seam
  candidate. Maps with all three new amounts at 0 are byte-identical to the v9 maps.
- **Blocked shots**: `blocked_shot` (both modes); the owner's replay replayed under the
  fix: blocked shots 1,935 -> 653.
- **Pictures** retaken for the changed screens (`docs/img/README.md`, "Retaken for 0.9.24").

Not run for this cut (the testing tiers): the remote desync sweeps, ASan, Clang and the
Windows and macOS local suites; tag CI builds and tests all three platforms.

## CI and packages

(Filled in after tag CI.)

## Signing

(Filled in after tag CI.)

## Publication

(Filled in at publication.)

## Known limits

- Only the Linux suite ran locally (the testing tiers); Windows and macOS are covered by
  tag CI.
- Taros mountains read as solid blocks: the original Taros mountain art is solid rock to
  every piece edge. Aramon and Taros plateaus are climbed by builders and infantry only.
- The seam check judges colour and tone at tile edges; a mismatch of shape or height with
  similar colours either side would pass it.
- Islands maps get no kits at the sizes tested (no inland room).
