# TAK Engine 0.9.3

0.9.3 follows 0.9.2. It fixes Trebuchets, which sat idle whenever scenery lay
between them and their target, and the same fault in every other unit that
shoots a plain arcing shell. It also moves macOS streaming from Apple's
deprecated SecureTransport to the OpenSSL that multiplayer already uses.

**Compatibility first.** The firing changes change the game simulation, so
0.9.3 uses network protocol **297** (replay format **15**, unchanged).
**0.9.3 does not play with 0.9.2 or any earlier version, and does not play
their replays.**

- **Everyone updates together.** A 0.9.3 player cannot join a 0.9.2 server, and
  a 0.9.2 player cannot join a 0.9.3 server: the connection is refused with a
  *protocol version mismatch* message. Update your game and tell the people you
  play with to update theirs.
- **Server operators:** update `takserver` to 0.9.3 when you announce the
  release. Until you do, 0.9.3 players cannot use your server; afterwards, 0.9.2
  players cannot. Crusades battle replays recorded by 0.9.2 on your server show
  as *Recorded replay version unsupported* and can no longer be watched in
  0.9.3.
- **Old replays:** a replay recorded by 0.9.2 or earlier needs a version with
  that replay's protocol. To keep watching 0.9.2 replays, keep your 0.9.2
  install (0.9.0 to 0.9.2 stay on the GitHub releases page); versions before
  0.9.0 are no longer offered for download and can be built from their git tags
  (see the [development guide](development.md)).
- **Everything else carries over:** settings, your data folder choice, key
  bindings, saved `.kmp` maps, generated-map recipes, mod packs and their
  ticks, and `ModCache/` downloads. No setting was added or changed.

## Arcing shells need no clear line

The engine tests for a clear line between a unit and its target before the unit
fires, and holds fire when scenery is in the way. The original game does not do
this for a lobbed shell: it admits a shot on range alone. Until 0.9.3 the engine
exempted a unit from the test only if one of its weapons was marked as
preferring a lob, so a plain arcing shell (marked Ballistic) was held to it. 42
shipped unit types shoot such a shell: the bows, the cannons, both Trebuchets
and the Arrow Tower and other arrow towers.

- **Decided per weapon.** A weapon needs a clear line only if it is a ranged
  weapon that is not Ballistic: flames, magic bolts and beams still need one. A
  unit with both kinds (the Cre'on Sage) tests each weapon on its own.
- **Blocked shots still apply.** A unit whose shells keep landing short walks to
  a clear spot as before (see [Blocked shots](user-guide.md#blocked-shots)).
- **No more blind holds.** A building, or a unit on Hold Position, whose
  approach weapon needs the line, and that has picked a target it has no line to,
  now drops that target and looks for another; the target search applies the
  same test from the same spot, so it is not picked again. Orders you give
  yourself are kept.

In a test duel between two computer players (Retail AI, Aramon against Creon,
seed `0x2222`) an Aramon Trebuchet held a Creon target for 47 minutes without a
single shot. Over 16 seeds of that duel, the times a unit held a target for over
a minute without firing fell from 80 to 4, and the one duel without a winner
within 90 minutes now has one.

## macOS streaming on OpenSSL

macOS streaming used Apple's SecureTransport, deprecated by Apple; FFmpeg 7.1's
backend for it needed a patch (0.9.1) to keep RTMPS uploads alive. The macOS
FFmpeg now uses the static OpenSSL 3.5.8 that the multiplayer TLS code already
builds (arm64 and x64, minimum macOS 14), like Linux. The patch is gone.

- OpenSSL never reads the keychain, so each connection attempt writes the
  system trust anchors that multiplayer TLS already imports to a private
  temporary file, hands it to FFmpeg as its CA file, and deletes it once the
  attempt has connected or failed. Certificate checking stays on and the host
  name is checked.
- Failure reasons and the 60-second reconnect reset from 0.9.1 are unchanged;
  the TLS CERTIFICATE reason now comes from OpenSSL's message.
- Windows and Linux are unchanged.
- **A trust anchor you added yourself is not used.** A certificate an
  administrator or you added to the macOS keychain (for example a company
  proxy's) is not honoured for streaming, because OpenSSL gets only Apple's
  built-in roots.

## For modders and mapmakers

- A Ballistic weapon no longer needs a clear 2D line to its target in any unit.
  `lobpreferred` no longer decides this; a modded unit with a Ballistic weapon
  now fires over scenery.
- No settings, map, recipe or package format changed.

## For contributors

- Sim (`66914175`, protocol 297): `Weapon::needsClearLine` decides per weapon
  (ranged, not `ballisticClass`); `slotClearShot` and `slotAims` use it for each
  slot, `repositionForShot` for the repositioned slot and
  `World::approachNeedsClearLine` for the unit as a whole (`tickCombat`'s
  closing in, `findTarget`, the flyer/structure hold and Legion's `reachPx`).
  `UnitType::lobs` is gone. New `ballistic_line_test` (Retail and Legion) fails
  before the change and passes after; the naval wall check now uses a
  line-of-sight weapon. Notes in [retail-engine.md](retail-engine.md).
- macOS streaming (`9da5f2d1`, `21f742ce`): `tak::net::systemRootsPem()`
  exports the system anchors; `src/video/stream.cpp` writes them to an
  `mkstemp` file per attempt and passes it as `ca_file`;
  `tools/ffmpeg-securetransport-nonblock.patch` is removed from the repo, the
  recipe stamp and every workflow's FFmpeg cache key, so CI rebuilds FFmpeg once
  on every platform. `streaming_rtmps` now uses a throwaway root CA and a
  CA-issued localhost certificate, refuses a certificate for another host name
  and one from an untrusted root. On macOS `stream_network_test --system-roots`
  checks the exported bundle. See [streaming.md](streaming.md).
- `--mpai` Inner Circle seed 1: all five gate hashes are unchanged from 0.9.2
  (Retail 60 s `d375d03a6c472773`, 300 s `387df8b0916a182e`; Legion 60 s
  `426ab0842b709c28`, 300 s `2d09d13d3ef0d324`; Retail AI 60 s
  `c5846d769e0a7d93`); the cross-compiler golden (`check-determinism`) stays
  `dcef618cd2e4d558`.

## Known limits

New with 0.9.3:

- **Every arrow tower and archer fires over scenery**, not only the Trebuchet.
  This matches the original but is a visible change in how towers and archers
  play.
- **Explicit attack orders to a building with no line** still hold without
  firing for a unit that needs the line; an existing `retail_script` check pins
  it.
- **One seed not explained:** in one test duel (seed `0x6666`) a Prismatic
  Mirror held an Aramon Trebuchet as its target for 120 s without firing even
  though a line existed. The cause was not investigated.
- **A Vermage on attack-move with Maneuver** dealt no damage in one test while
  taking 40 hits. It behaves the same in 0.9.2; it may be its own mana. Not
  investigated.
- **macOS streaming trust:** keychain certificates you added yourself are not
  honoured (above). No automated test shows a chain to a real system root being
  accepted; the evidence is a manual handshake with a public site on an Apple
  Silicon Mac. macOS x64 was not run with the new FFmpeg configuration.

Everything listed under Known limits in the
[0.9.2 notes](release-0.9.2-notes.md#known-limits) still applies, except that
macOS streaming no longer uses SecureTransport and that Trebuchets and other
arc shooters no longer hold fire for lack of a line.

## Compatibility

Protocol **297** (was 296), replay format **15**, generator version **9**,
campaign payload **4** and Crusades SQL schema **9**, all but the protocol
unchanged from 0.9.2. Checked in source against `v0.9.2`: `kNetVersion` in
`src/net/protocol.h`, `kReplayFormat` in `src/net/replayhdr.h`, the map
generator's `formatVer` (`src/tnt/mapgen.h`), the campaign payload's `kVersion`
(`src/net/crusades.h`) and `kSchemaVersion` (`src/server/crusades/store.cpp`).

- **Multiplayer.** A server accepts a client whose protocol equals its own (and
  whose original game files match the server's); the release number is not
  sent. So **0.9.3 clients and servers play only with each other**.
- **Replays.** Playback requires the recording's protocol to equal the
  engine's, so 0.9.3 plays only 0.9.3 replays; a 0.9.2 replay (format 15,
  protocol 296) is refused. Crusades battle replays follow the same rule.
- **Maps, mods and settings.** Saved `.kmp` maps, generated-map recipes,
  `ModCache/` downloads, pack folders and settings files carry over unchanged.

No new runtime dependencies or retail assets are included. See the
[release validation report](release-0.9.3-validation.md).
