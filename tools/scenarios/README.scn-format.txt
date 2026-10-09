.scn scenario format (version 1)
================================

Parser, world builder and order feed: tools/legion_scn.h. Orders go through
tools/legion_issue_selection.h, which mirrors the client's HUD commands and its
64-per-tick uplink. Checked by the issue_selection_test ctest.

One directive per line. '#' starts a comment (not inside an ascii map block).
Coordinates are map cells (16 px), x then z; points may be fractional. A file
never names a pathfinding mode: the runner builds it once per mode.

  scn 1                         required first line
  name NAME
  ticks N                       run length (default 3000)
  seed N                        game seed (default 7)
  players N                     1..8 (default 2); player p starts on team p
  team P T
  weapons on|off                default for groups (default off)
  explored all|none             pre-explore the nav map (default all)
  wanderers on|off              fbi types keep (default) or lose Standby_wander,
                                the home-pull that re-orders idle wanderers every
                                240 ticks (legion_scenario --wanderers overrides)
  crusades on|off               load the Crusades balance overlay for fbi types
                                (default off)
  uplink R                      command window: at most 512 commands
                                outstanding, round trip R ticks (default: none,
                                only the server's 64-per-tick drain)
  probe approach                the runner also reports the approach.* keys: per
                                unit, the first tick Legion holds it at its
                                approach point (an unreachable goal's nearest
                                reachable spot) and the tick its orders empty.
                                Meant for scenarios whose goal is unreachable.

Map (exactly one):
  map ascii                     rows follow, closed by a line 'end':
                                  .  ground (height 100; sea level is 64)
                                  #  wall (1x1 blocking map feature)
                                  ~  deep water (20)   ,  shallow water (56)
                                  ^  high ground (200)
  map flat W H                  open ground
  map gen1 RECIPE               tak::mapgen::generate("~gen1~...")
  map snapshot FILE             a .tnt or .kmp, relative to the .scn file
  wall X Z W H                  ascii/flat only: a block of wall cells
  height X Z W H V              ascii/flat only: set terrain height V

Types:
  type NAME mover FOOT TURN ACCEL SPEED [opts]   ACCEL px/tick^2, SPEED px/tick
                                (SPEED 0: the Keep, canmove with no velocity)
  type NAME flyer FOOT TURN ACCEL SPEED [cruise=80] [opts]   (VTOL standby)
  type NAME boat  FOOT TURN ACCEL SPEED [depth=13] [opts]
  type NAME hover FOOT TURN ACCEL SPEED [opts]
  type NAME structure FOOTX FOOTZ [opts]
  type NAME fbi UNITNAME                         the install's unit (data-gated)
  type NAME roster [opts]       the retail roster's quartiles, cycled by member
                                index: sight 256/320/400, turn 1000/2500,
                                footprint 2/3, speed 1.6..2.6; gun 160,25,1
  opts: sight=N hp=N footz=N turninplace=N gun=RANGE,DAMAGE,RELOAD
  An armed synthetic type without gun= carries the fixtures' gun (120,100,0.1).
  An unarmed fbi type is a copy outside the registry (legacy nav grid).

Groups (spawned in file order, members in row-major order):
  group NAME OWNER TYPE COUNT rect X0 Z0 X1 Z1 [pitch=N] [opts]
                                fills [X0,X1) x [Z0,Z1) every pitch cells
                                (default: largest footprint + 1)
  group NAME OWNER TYPE COUNT cells X,Z X,Z ... [opts]
  opts: squad=fN|gN             Alt+N / Ctrl+N pressed at tick 0 (one press
                                per number, over every group naming it)
        weapons=on|off

Orders (stable-sorted by tick; issued before World::tick at that tick):
  at TICK move|fight|patrol SEL X Z [queue]
  at TICK move|fight|patrol SEL @GROUP [queue]   the group's centroid then
  at TICK attack|guard SEL @GROUP [queue]        its first live member
  at TICK stop SEL
  at TICK squad SEL fN|gN|none [append]
  SEL is a comma list of groups (selection order = list order, then spawn
  order) or 'all'; one order's selection belongs to one player.
  move = right-click on ground (Legion: surface movers share the point; flyers,
  bodies over 8 and every unit in other modes keep their offset from the
  selection centroid, clamped to +-60 px per axis). fight/patrol/guard = the
  armed F/P/G order, attack = right-click on an enemy, stop = the Stop key.

Shapes (for the runner's metrics; never affect the world):
  gate NAME X0 Z0 X1 Z1         a segment
  line NAME X0 Z0 X1 Z1         a segment
  region NAME X0 Z0 X1 Z1       a rectangle

Start offset: the builder shifts every spawn by 0, +1 or -1 cells on both
axes (BuildOptions::offset); orders, walls and shapes stay put.

Data gating: fbi types, gen1 and snapshot maps need an install (--data, or a
mounted view). Without one, build() returns Built::skipped =
"needs --data: <reason>" and builds nothing.
