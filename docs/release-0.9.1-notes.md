# TAK Engine 0.9.1

0.9.1 follows 0.9.0. It fixes the bugs players reported in their first 0.9.0
games: units that aimed at their targets' feet, ships stuck in their yard, and
game speed that stayed at +4. It also changes how new units leave their
building, so that it works as in the original game, and fixes YouTube streaming
on Linux, macOS and Windows. On Windows the game now draws with OpenGL by
default.

**Compatibility first.** The aiming and the way new units leave their building
change the game simulation, so 0.9.1 uses network protocol **295** (replay
format **15**, unchanged). **0.9.1 does not play with 0.9.0 or any earlier
version, and does not play their replays.**

- **Everyone updates together.** A 0.9.1 player cannot join a 0.9.0 server, and
  a 0.9.0 player cannot join a 0.9.1 server: the connection is refused with a
  *protocol version mismatch* message. Update your game and tell the people you
  play with to update theirs.
- **Server operators:** update `takserver` to 0.9.1 when you announce the
  release. Until you do, 0.9.1 players cannot use your server; afterwards, 0.9.0
  players cannot. Crusades battle replays recorded by 0.9.0 on your server show
  as *Recorded replay version unsupported* and can no longer be watched in
  0.9.1.
- **Old replays:** a replay recorded by 0.9.0 or earlier needs a version with
  that replay's protocol. To keep watching 0.9.0 replays, keep your 0.9.0
  install (0.9.0 stays on the GitHub releases page); versions before 0.9.0 are
  no longer offered for download and can be built from their git tags (see the
  [development guide](development.md)).
- **Everything else carries over:** settings, your data folder choice, key
  bindings, saved `.kmp` maps, generated-map recipes, mod packs and their
  ticks, and `ModCache/` downloads. A Renderer you chose yourself in Options
  stays chosen; only **Auto** on Windows picks differently (below).

## Aiming

- **Units aim at the middle of the body.** The point a unit aims at on its
  target is now worked out as in the original: the middle of the target's
  pieces as they are drawn, turned with the target. Before, it was the middle
  of the model as authored, which for most units lay too low and for some below
  their feet, so spears, arrows and other shots that fly in a curve hit the
  ground in front of the target. In the reported game, 727 spears that Zhon
  Hunters threw at other Hunters all missed, and 4% of their shots hit
  anything; with the new aim, 89% hit in the same situations. 110 of the 189
  shipped unit types aim differently.
- **Lightning at the Creon Submersible.** A Thirsha's lightning at a parked
  Submersible still misses often, as in the original: the Submersible's aim
  point lies off its centre and, for some headings, outside the cells it stands
  on, so whether a bolt hits depends on which way the Submersible faces. In the
  reported game all 47 bolts missed; the original's own routine, run on those
  same shots, hits 17 of them, and so does 0.9.1.

## New units leaving their building

- **Without a rally point, as in the original.** A new unit moves off the pad
  to a spot a short way from the building, on a ring around it, and waits there.
  Before, it was sent a fixed distance straight out of the door. Give the building a **move** or
  **patrol** order to set a rally point, as before; new units then go straight
  there.
- **Ships only pick water they can reach.** A ship's spot must be connected to
  the water it was built in; if no spot on the ring is, it moves to the
  farthest free spot it can reach, clear of the yard. Ships no longer sit on the
  pad of a yard by a small lake, holding up the yard.
- **The computer players do not build yards on ponds.** Our AI levels (Easy to
  Absurd) place a shipyard only on a body of water of at least 256 cells, so
  not on a landlocked pond. The Retail AI places yards as the original did.
- **Flyers fly out.** A new flyer with no rally point flies to a random spot on
  a circle a few hundred pixels around the factory, on any side of it, as in
  the original. Before, it stopped a short way in front of the factory. With a
  rally point it flies straight there, with no stop on the way.
- **Under Legion, each new unit gets a spot of its own.** With Legion
  pathfinding, every new unit with no rally point takes a free spot around the
  building: nearest the door first, on alternating sides, never in front of the
  door, only where it can walk to, and further out once the near spots are
  taken. A slow unit still walking to its spot is given time to get there. In
  a test queue of 100 Catapults from one Keep, none stopped in the door lane.
  Retail pathfinding uses the original's spots.

Units built in the field by a mobile builder (such as the Zhon Beast Handler)
leave as before; the original gives them no such order.

See [Building](user-guide.md#building) in the user guide.

## Game speed

Raising the speed to +10 now takes effect at once. A player or spectator whose
game had paused for a moment (while it loaded, after switching to another
window, or with a menu open) could play at about +4 for most of a minute
before reaching +10: the game counted its own pause as a slow network and
asked for more buffered game time than the server ever sends ahead. Only the
game's pacing changed: every player still sees the same game.

## YouTube streaming

**Linux (Intel and AMD).** Hardware encoding now uses the VA-API library
installed on your system (`libva`) and its driver. The game used to carry its
own, older copy of libva, which could not start current drivers (Intel's on
Fedora 44, for example) and did not look in Fedora's RPM Fusion driver folders,
so the stream fell back to the CPU (**LIVE - CPU FALLBACK**) even where
`vainfo` worked. Without libva the game streams on the CPU as before; libva is
not a package dependency. To see what libva does, start the game from a
terminal with `LIBVA_MESSAGING_LEVEL=2`
([Intel and AMD hardware encoding on Linux](streaming.md#intel-and-amd-hardware-encoding-on-linux)).

**macOS.** Streams from a Mac now reach YouTube. Every upload used to stop
after a few frames (RECONNECTING, then FAILED), leaving only the local copy: a
bug in FFmpeg's macOS secure-connection code froze the upload for 5 seconds the
first time it checked for a reply from YouTube. The game's built-in FFmpeg is
patched.

**Every system.**

- **The panel says why a stream fails:** **RECONNECTING 1/3 - CONNECTION
  DROPPED**, and after the last retry **FAILED - TLS CERTIFICATE NOT
  TRUSTED**, **FAILED - CANNOT CONNECT - CHECK KEY, NETWORK** or **FAILED -
  CONNECTION DROPPED - CHECK NETWORK**
  ([Connection details](streaming.md#connection-details)).
- **Fresh retries:** a stream that stayed up for a minute gets three new
  retries when the connection drops, instead of sharing three across the whole
  stream.
- **Large windows keep up.** A stream smaller than the game window is shrunk
  on several threads, and on the graphics card under every Windows renderer.
  Streaming a 7680 × 2160 window at MAX 3840, 60 FPS and 6,000 Kbps with NVENC
  used to encode about 18 frames a second, and under Direct3D 9 (Windows' old
  default) it cut the game from 240 FPS to about 30. It now encodes all 60
  frames a second at about the set bitrate, with the game at 240 FPS, under
  Direct3D 9, Direct3D 11 and OpenGL alike. On Windows the encoder also no
  longer oversleeps between frames.
- **The hardware cursor stays on.** Streaming, or just opening the streaming
  panel, used to switch to the cursor the game draws itself. Now the system's
  hardware cursor stays on your screen, and the stream draws the same cursor
  into the video: the same art, animation frame, tint and Cursor Size, at the
  pointer, scaled to the stream, clipped at the picture's edges, and left out
  while the pointer is outside the window. With **Hardware Cursor** off,
  nothing changes ([The cursor](streaming.md#the-cursor)).
- **Which cursor is in use.** Options now shows beside Hardware Cursor what the
  game gave the system, such as `OS CURSOR 64X100`, or `UNAVAILABLE - SOFTWARE`
  when the system refused that size and the game is drawing the cursor itself.
  The log says the same.

Streaming is client only: none of this changes who can play together.

## Windows: OpenGL by default

With **Options → Graphics → Renderer** on **Auto**, Windows now draws with
OpenGL instead of Direct3D 9, because some of the game's drawing shortcuts work
only with OpenGL. Auto passes over Windows' software OpenGL ("GDI Generic", on a
machine with no graphics driver) and Microsoft's OpenGL compatibility layer,
and then tries Direct3D 11, then Direct3D 9. The Renderer row shows the pick,
for example **Auto (OpenGL)**, and the log has a `renderer:` line for each
backend Auto tried. Linux (OpenGL) and macOS (Metal) pick as before; their Auto
row names the pick too.

- **Which is faster.** On the test machine (RTX 5070, 7680 × 2160), OpenGL drew
  a 2,000-unit view about 11% faster than Direct3D 11 and 17% faster than
  Direct3D 9; with 10,000 units on screen it was about 10% slower than
  Direct3D 11. If you play very large battles, try **Direct3D 11**.
- **To switch back**, choose **Direct3D 11** in the Renderer row, or
  **Direct3D 9** for exactly what earlier versions used, and restart the game.
  If the game does not reach the menu, close it and set `renderer =
  direct3d11` (or `renderer = direct3d` for Direct3D 9) in `settings.ini`
  ([The game will not start, or shows a black screen](user-guide.md#the-game-will-not-start-or-shows-a-black-screen)).
- A renderer you chose yourself is used as it is. If it cannot start at all,
  that session uses Auto and the main menu says so.

See [Renderer](user-guide.md#renderer) in the user guide.

## For modders and mapmakers

- A unit's aim point now comes from its model pieces as drawn in their current
  pose, as in the original. A modded model whose SweetSpot piece moves or turns
  (or whose body pitches or rolls) is aimed at where that piece is drawn. Where
  a model has two objects with the same piece name, the first one counts.
- Factory output: a building's rally (its move or patrol orders) is handed to a
  new unit as is; with none, the unit parks around the building at the
  original's distance, which grows with the building's footprint.

## For contributors

- SweetSpot (`fbc98cf3`, `14cb29ad`, protocol 295): `retailPieceDrawnCenter`
  (`src/sim/retailpiecepose.h`) bounds each piece's vertices posed by the piece
  chain plus body attitude (retail `4dd2a0` over the buffer the draw path
  `4ee620` poses in place), and `UnitType::scriptPieceVertices` replaces
  `scriptPieceCenters`. The per-unit cache and branchless rounding (`adaac1ca`)
  are bit-identical. New ctest `sweetspot`; `retail_script_test --bounds` /
  `--local-bounds`; `tools/re/check_captured_aim.py`. Evidence in
  [retail-engine.md](retail-engine.md).
- Factory output (`bf45dc79`, `71214f5a`, `4c22fbe3`, `fe0dfdcb`): retail
  GetBuilt (`0x402220`) rally-else-PARK for every structure output, the PARK
  ring (`407ca0`, `src/sim/retailpark.h`) kept to the birth point's component,
  PARK's flight branch for flyers held until the flight arrives. Our AI's
  pond-yard refusal (`src/ai/ai.cpp`, 256-cell water body). Mobile producers
  keep their exit step. ctest `shipyard_park`, `production_exit`.
- Legion PARK spread (`6269cd36`, `647668a9`): `World::legionParkSpot`;
  Retail mode bit-identical. New ctest `legion_park`.
- Client pacing (`fb03e3be`, `db72685b`): `ArrivalJitter` and
  `jitterTargetTicks`/`flowBufferLimit` in `src/net/pacing.h`; debug hook
  `TAK_STALL_AT=<tick>:<ms>`, `tools/speed_stall_check.sh`, ctest `pacing`.
  Nothing on the wire changed.
- Streaming: `tools/vashim.c` (a static libva that dlopens the system
  `libva.so.2`/`libva-drm.so.2`; ctest `vashim_*`; libdrm and Meson no longer
  built), `tools/ffmpeg-securetransport-nonblock.patch` (Darwin only; ctest
  `streaming_rtmps`), `src/video/rgbascale` (threaded RGBA scaler),
  `src/client/streamcapture_d3d` (Direct3D 9/11 GPU-shrunk capture),
  `src/video/streamcursor` (cursor stamping). The FFmpeg cache keys and the
  recipe stamp include `tools/vashim.c` and the patch, so CI rebuilds FFmpeg.
- Windows Auto renderer (`f1fd241c`, `cc164d74`, `061af064`, `679f00e1`):
  `src/client/renderdriver.h`; OpenGL is judged on a hidden window first, and
  the probe keeps the GL library loaded. Debug hooks `TAK_RENDER_FAIL=<id>` and
  `TAK_RENDER_FAKE_GL=<GL_RENDERER>[|<GL_VENDOR>]`; ctest `renderdriver` now
  runs in all three CI builds.
- Screenshots (`6c84dee1`): the debug/test `screenshot()` reads an explicit
  rectangle, so a capture in the frame of a fullscreen switch no longer
  overruns its buffer. Render-study hooks `TAK_RENDER_FULLSCREEN` and
  `TAK_RENDER_CAPTURE_EVERY` (`d9389129`, debug).
- `--mpai` Inner Circle seed 1: at 60 s unchanged from 0.9.0 (Retail
  `d375d03a6c472773`, Legion `426ab0842b709c28`, Retail AI `c5846d769e0a7d93`);
  at 300 s Retail `a470242be92a0e14` (was `b69b4e0842ba4eef`), Legion
  `a65a48d7a63825ae` (was `0da23f54df7a1b9d`). The cross-compiler golden
  (`check-determinism`) stays `dcef618cd2e4d558`.

## Known limits

New with 0.9.1:

- **Legion factory spots.** In the review's test of 100 Catapults built at 20×
  speed, one stayed held behind the Keep; a unit's spot does not avoid the lane of a
  neighbouring building's door; and once all 16 layers of spots are full, a new
  unit falls back to leaving clear of the building. Legion units leaving a
  factory still stand and wait more than Retail's, as below.
- **Ships and yards.** The size of pond our AI refuses (256 cells) is ours; the
  original's AI has no such test, so the Retail AI can still put a yard on a
  pond. When a ship's ring has no reachable spot, the farthest reachable spot is
  our own fallback, not the original's.
- **Aiming cost.** The new aim point costs more to work out than 0.9.0's; it was
  made about four times faster before release, but no large-battle benchmark
  compared 0.9.1 with 0.9.0.
- **The hardware cursor.** On the Windows and macOS machines where it was
  reported to feel like a software cursor, the hardware cursor was in use the
  whole time and the game drew nothing itself; no cause in the engine was
  found. The Options row and the log now say which cursor runs, for a future
  report. A large **Cursor Size** (4× is a 64 × 100 image) is handed to the
  system as is; if the system refuses that size, the row reads `UNAVAILABLE -
  SOFTWARE` and the game draws the cursor.
- **Streaming capture.** Direct3D 9 still waits for the graphics card on each
  (now small) readback, and Metal and the software renderer still read back the
  whole window on the render thread; OpenGL and Direct3D 11 do not wait.
- **The Windows renderer** was measured on one NVIDIA machine. On other
  drivers OpenGL may be slower than Direct3D 11; switch in Options.
- **macOS streaming** still uses the system's SecureTransport for its secure
  connection (now patched); moving it to OpenSSL is planned, not done.

All of 0.9.0's known limits carry over:

- **The Shift order-trail preview** of a queued area clear uses what your side
  sees now, not the explored ground the order will use.
- **Flying reclaimers** reach their targets with a plain move and still
  start from their old distance; they work for the original's time.
- **The fade** of a reclaimed object is not drawn: it stays as it was for
  about a second, then disappears.
- **Legion units still stand and wait more than Retail's**, in crowds, at
  corners and in rings, though they usually arrive sooner. This is cosmetic.
- **Legion attackers through a narrow gate deal more damage than Retail's**:
  in the test layouts from about 1.2 to 2.2 times as much.
- **Power codes go through the game simulation like orders**, so their effects
  are part of every player's game (and of replays); the original applied a code
  only on the typer's computer. +DoubleShot and +HalfShot change everyone's
  damage.
- **Fog of war: Full Vision does not switch line of sight off at the start.**
  Your units do not attack what they cannot see until **+LOS** or **+NowISee**
  is typed.
- **UI Font: Classic is close to, but not pixel-identical with, 0.8.7's
  interface.**
- **The campaign book's chapter numeral** still touches the bottom of "APTER"
  in CHAPTER, as in the original game.
- **Legion bridges are still a lottery at about 100 units.**
- **A stall on the server's side is charged to the players**, and a
  reconnecting player who hitches while catching up is charged as any other
  hold.
- A stationary unit choosing its own targets can keep ignoring an enemy it once
  found blocked. The Legion 10,000-unit workloads still struggle with
  congestion and the largest run over the tick budget; in mixed-10000 the
  flyers hover over a slow ground army. Moving the game window between a
  Retina and an ordinary display does not rescale UI Scale. In the remote test
  sweep, Retail `h-cramped` ends early because the idle seat starts with only
  its Monarch.
- **The Retail AI** is not a replay of a 1999 game (this engine's world, orders
  a tick later, its own random numbers), weighs targets against this game's
  unit cap and has not been tested at it, is weaker than Normal (a Taros Retail
  AI can fall far behind), and in campaigns takes its start positions from the
  mission map's start markers (inferred).
- **Our six levels share identical built-in tables**, so `ai/takengine.txt`
  sets the same unit weights and limits for all of them (they still differ in
  behaviour and limit scaling).
- **TEST IN GAME has not yet been tried by hand on Windows or macOS.**
- The Retail AI's server log still says `DEFAULT AI profile ai/DEFAULT.txt`, and
  the Cartographer's tab still reads **DEFAULT AI** (log and label wording
  only).
- **Old always-on archives are not moved for you**, and the game does not warn
  about a `.hpi` left directly in `mods/cosmetic/` (see 0.8.14's
  [notes](release-0.8.14-notes.md#moving-your-always-on-archives)).

## Compatibility

Protocol **295** (was 294), replay format **15**, generator version **9**,
campaign payload **4** and Crusades SQL schema **9**, all but the protocol
unchanged from 0.9.0. Checked in source against `v0.9.0`: `kNetVersion` in
`src/net/protocol.h`, `kReplayFormat` in `src/net/replayhdr.h`, the map
generator's `formatVer` (`src/tnt/mapgen.h`), the campaign payload's `kVersion`
(`src/net/crusades.h`) and `kSchemaVersion` (`src/server/crusades/store.cpp`).

- **Multiplayer.** A server accepts a client whose protocol equals its own (and
  whose original game files match the server's); the release number is not
  sent. So **0.9.1 clients and servers play only with each other**: no 0.9.0
  or older client can join a 0.9.1 server, nor a 0.9.1 client an older server.
- **Replays.** Playback requires the recording's protocol to equal the
  engine's, so 0.9.1 plays only 0.9.1 replays; a 0.9.0 replay (format 15,
  protocol 294) is refused. Crusades battle replays follow the same rule.
- **Maps, mods and settings.** Saved `.kmp` maps, generated-map recipes,
  `ModCache/` downloads, pack folders and settings files carry over unchanged;
  no settings key was added or renamed.

No new runtime dependencies or retail assets are included: on Linux the game
uses the system's libva only when it is installed, and no package depends on
it. See the [release validation report](release-0.9.1-validation.md).
