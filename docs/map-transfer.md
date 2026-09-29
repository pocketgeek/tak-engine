# Multiplayer map availability and transfer

Map verification was introduced in protocol 193 (0.7.10 uses protocol 194).
The selected map is checked before starting. The host supplies a canonical
package fingerprint (SHA-256). The server checks its installed map/cache, requests the
host's copy when missing or different, and offers that verified copy to all players.
The game cannot start until the server and every connected player/spectator have the
same package. Late spectators and reconnecting players verify/download before loading
and catching up. A matching filename alone is never sufficient.

Packages contain the TNT, optional OTA (starts, wind, gravity, scenario settings),
referenced terrain JPGs, and map feature definitions with their burn/death chains,
sprites, and palettes. Unit definitions, build lists, and scripts are not transferred;
the existing gameplay-data checks still require compatible game data/mods.
Map-specific resources are mounted only for the selected game, including on a server
hosting multiple rooms. Downloaded map resources do not alter the base-game handshake
or the generated-map source data.

Transfers use the existing TCP connection in sequential 64 KiB chunks, with bounded
send queues and a 256 MiB package limit. The receiver verifies the complete digest,
resource paths/types, geometry dimensions, and dependencies before accepting it.
Partial transfers are not installed. Leaving/reconnecting restarts an incomplete
transfer; completed copies are reused. No received path is extracted onto disk.

Verified copies are saved under `<data root>/MapCache/`: `.takmap` is the checked wire
package and `.kmp` is its archive for the map catalog. Map-picker entries have the
original map name plus a fingerprint, so different versions remain separate and no
installed map is overwritten. They are selectable in later games, including when that
machine becomes the host. Replays record the package digest (replay format 9), so they
use the exact cached copy even if a different map with the same name is installed.
Map caches are local to each client's/server's configured data root.

Generated recipes still need no terrain download. At match start each participant
saves a reusable `.kmp` under `Maps/`; see [random map generation](random-map-generation.md).
The data directory must be writable for persistent saving. A client reports a cache
save failure in its log; it can still play using its verified in-memory copy.

Validation: `map_transfer_test` checks canonical identity, resource isolation,
cache/catalog reuse, malformed packages, checksums, and chunk ordering. With a data
root argument it also reopens a saved generated map and checks terrain and starts.
Its `--network PORT HOST_ROOT PEER_ROOT [MAP]` mode exercises real server transfer,
late spectating, and matching simulation ticks with separate installations.
