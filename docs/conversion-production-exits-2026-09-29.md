# Conversion targets and production exits

## Harpy targeting

An existing attack order checked whether its target was alive but did not
consistently check whether conversion had made it friendly. The shipped Harpy
reproduced the stale order and aim in both standard and Crusades balance.

Automatic attacks and converter attacks now require hostility throughout the
order. Losing that eligibility clears the script aim, conversion progress and
current attack leg while preserving queued movement. Ordinary explicitly ordered
friendly fire remains available to non-converters. A charm impact also checks
current allegiance, so an already launched spell cannot steal a converted ally.

The shipped-unit regression covers explicit and automatically acquired targets,
conversion to the caster's owner or an ally, queued movement, late charm impacts,
and acquisition of another nearby enemy in both balance modes. It calls the same
capture transition used by live conversions, without relying on a random charm
roll to time the ownership change.

## Factory and infinite-queue exits

The reported Aramon Keep/Swordsman/no-rally case reproduced on flat terrain in
both balances: two completed Swordsmen, then no further production through
18,000 ticks. The second Swordsman had finished its movement while still blocking
the next unit's birthplace.

Exit destinations were only about 60 pixels away. The existing retail navigator
can finish short of a crowded destination by `50 / halfCellTicks` cells. That
legitimate movement completion could leave a unit inside the production area.
The exit search could also retreat toward the birthplace while looking for an
unoccupied destination. Reserving earlier outputs' destinations alone did not
resolve the reproduced stall.

Production now places its exit goal beyond the mobile placement exclusion and
that stopping tolerance, with cell-rounding margin. The outward search retains
that minimum separation from the birthplace and considers nearby active movement
goals as well as current bodies. The change is in shared production destination
selection; navigation, collision and the navigator's arrival rules are unchanged.
Rally orders still follow the exit leg. Terrain or a genuinely full area can still
prevent production; this does not bypass placement or overlap units.

The data-backed `production_exit` regression runs 40 cases: standard/Crusades,
four orientations, 24 queued Keep Swordsmen, infinite Huntress production of
Beast Handlers, infinite Beast Handler production of Hunters, Sea Fort Scouts,
and infinite Beast Lord production of Rocs.
It checks actual build menus and scripts and uses no rally point.

Protocol **202** covers both simulation changes. Released 0.7.15 uses protocol
201; update the client and server together when testing this checkout.

## Validation

Release 125/125, optimized Debug 130/130 and Clang ASAN/LSAN 126/126 pass
(381 total). The initial new production fixture incorrectly paired some Zhon
builders and units; the corrected fixture checks the actual build menus and was
rerun in all three builds. All other full-suite checks had already passed.
Sanitizer tests use dummy SDL video/audio and explicit SDL DBus shutdown, with
no leak suppressions.

All 40 production-exit hashes agree across the three builds when given the same
retail data directory. The configured CTest suites use different data roots;
comparing their raw hashes initially found four standard-balance Keep differences,
which disappear with identical input data.

The local host/referee/late-observer scenario passed 330 matching ticks with
hash `6d9f4eb1ff38520c`. GCC/Clang O0/O2/O3 deterministic-math checks agree on
`dcef618cd2e4d558`; ARM cross-build legs lacked target headers and were skipped.
