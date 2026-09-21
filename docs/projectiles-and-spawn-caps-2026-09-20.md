# Projectile models and benchmark unit limits

Stress setup and benchmark plans now reserve each player's positive `totalAllowed`
limit, including the starting monarch. The deterministic roster skips exhausted
types and fills with eligible types. Scheduled waves check the live type count
again, so intervening construction cannot exceed the limit. Under-construction
units count; dead units free their slot. All monarch footprints are reserved before
stress armies fill the map, preventing earlier armies from consuming later
monarchs' valid positions on cramped terrain. Pathfinding is unchanged.

The setup changes require network protocol 172. Local clients and servers were
rebuilt together in `build`, `build-dbg`, and `build-o2`; remote installations were
not changed.

The projectile audit found two separate rendering faults:

- Instant-hit weapons with an FBI `model` were displayed as generic glowing
  beams. Their cosmetic shot now draws that model while preserving instant damage
  timing. Examples in both balances include the Veruna Crossbowman (`araarrow`)
  and Harpoon Ship (`araharp1`); the shared path also covers the Rolling Tower's
  anti-air arrows and the standard-balance Centaur's bolt.
- Veruna Ballista's `verbal1` mesh was already rendering, but backward. Both
  `verbal1` and `verbal1_vet` have their head along authored -Z, unlike the +Z
  arrow/harpoon meshes inspected. The common model renderer now accounts for
  these two assets' opposite orientation. This does not add veteran-model
  selection where the weapon loader does not already provide it.

Validation:

- All 37 CTest tests passed in each of the three builds (111 passes).
- Asset-backed placement tests check stress and planned benchmark type counts
  in standard and Crusades balance. Production tests cover per-player isolation,
  under-construction units, repeated wave entries, and replacement after death.
- Both balances initialized exactly 16,000 living units in the patrol fixture
  (`simperf --units 15992`, plus eight monarchs). Only one tick was run: this is
  a workload-size check, not a sustained performance claim.
- Native compiler/optimization determinism checks retained golden hash
  `8adc4762a852fadd`; ARM toolchain legs were unavailable.
- Two optimized-debug Crusades multiplayer runs reached tick 600 with no errors
  and identical hash `b879f25ebe4532bf`.
- Controlled debug-client shots confirmed the Ballista bolt points toward its
  target and the Crossbowman uses a solid arrow mesh. GDB captured the actual
  cosmetic model name and geometry; temporary screenshots/logs are under
  `/tmp/tak-{ballista,crossbow,harpoon}-*`. These are renderer checks, not a claim
  of a frame-for-frame comparison against the retail executable.

`TAK_PROJECTILE_TEST=<unit id>` with debug `--firetest` provides a repeatable
single-shooter fixture. Release ignores this developer hook.
