# Milestone 11: territory history and retained replays

This is a modern TAK-Engine archive policy, not a recovered Boneyards history
protocol or a reconstruction of historical capture/ranking formulas. Tactical
simulation and the retail pathfinding rules are unchanged.

## Results remain authoritative

The schema-8 store maintains an indexed, immutable `territory_battle_history`
projection of verified terminal results. It records the campaign, territory and
server result time and backfills existing verified results on migration. History
pages use the descending `(recordedUnix, battleId)` key, with at most 32 rows;
cursors must identify a real result in the same campaign and territory. Page
reads validate the projection against the immutable battle/result records in one
database snapshot. Unplayed cancelled or expired offers have no verified result
and do not appear. Verified draw, abort and failure outcomes remain visible and
are labeled as those outcomes rather than victories.

The original verified result owns the participants, outcome, winners, final tick,
state hash and player statistics. Archive reads and replay downloads neither
recompute credit nor invoke campaign rules. A replay is optional supporting
material. Deleting a recording, rejecting its header or failing a download does
not delete history or change its result. Missing historical capture formulas are
still unknown; a replay link supplies no authority to invent them.

## Access and wire boundary

Network protocol 211 and campaign payload 4 append history and replay messages.
An authenticated account enrolled in a campaign can read that campaign's verified
territory history and retained recordings. Enrollment in another campaign grants
no access. Existing active/private battle-status reads remain participant-only.
There is no public replay directory listing and no client-selected server path.

History sends account IDs, dates, map identifiers, verified outcome/statistics
and optional digest/size/header metadata. It never sends database replay paths,
launch capabilities or room tokens. Downloads use an opaque battle ID and bounded
64 KiB chunks, pinned to the expected digest, size, request and offset. Ordinary
campaign reads retain their 32-per-second quota; replay reads have a separate
64-per-second quota, while the client paces pulls at least 20 ms apart.

## Retention, validation and playback

The server retains its existing replay filename convention in the configured
replay directory. Read access reconstructs that exact name from the battle ID
and the immutable SHA-256; arbitrary stored names, symlinks, nonregular files and
files larger than 512 MiB are refused. The server opens a safe file handle,
checks a bounded header and reads at most one chunk per request. It does not
rescan a large replay body on the server's simulation thread.

Optional archive metadata means a retained recording has a compatible header and
size, not that its whole body was freshly hashed on the server. The client streams
to a temporary file in its user replay cache, computes the full SHA-256, and
publishes atomically only after all bytes match. Interrupted, malformed, changed,
corrupt or unavailable downloads are discarded. Cached recordings are verified
before reuse. File parsing then validates the recording structure and rejects
trailing or malformed commands/events.

`Watch Replay` uses the same replay loader and GameView as ordinary replay
playback. It preserves the authenticated campaign connection and strategic view;
no multiplayer client is attached to the playback view. Output ownership is
released before the replay opens its audio device and restored after playback,
including on loader failure. This keeps sound working with an exclusive output
backend. Press Esc or its return button to restore the same territory and history selection. Disconnects and
validation failures use the existing strategic error/reconnect flow.

The viewer requires matching gameplay data and the recorded map package. A
verified local cache is accepted; an installed map is usable only if rebuilding
its package matches the recorded digest. An unavailable custom map fails with an
explanation and leaves history available. No original game assets are bundled
into the archive feature. Format-9 recordings from protocol 210 remain accepted
under 211 because only strategic message types changed; older incompatible
protocols are still refused. Dependencies remain static and cross-platform.

## Validation

The store, codec/client, artifact access, authenticated service and actual UI
workflows have separate regression gates. See the tests registered in CMake:
`crusades_history`, `crusades_replay_files`, `crusades_protocol`,
`crusades_client_network`, `crusades_network_service`,
`crusades_history_network` and `crusades_history_ui_network`.

All Release and Debug targets were rebuilt. The focused Release sweep passed
32 tests; the Debug campaign/replay sweep passed 25. Four additional connection,
pathfinding and retail-script/visual regressions passed. The two existing actual
SDL campaign/matchmaking workflows passed unchanged. After the audio transition fix, all 31 selected
Release archive/audio gates and six Debug audio gates passed, followed by the
final isolated SDL archive workflow. Store/history/artifact/service
checks (750 total) and the final 307-check real MpClient harness passed ASan,
UBSan and leak detection. The strict protocol suite passed 1,429 checks.

The real-server archive gate creates verified referee results, restarts the
process, checks enrollment/participant isolation and cursor paging, downloads
exact replay bytes, and audits result/event/rules rows before and after missing,
header-corrupt, body-corrupt and symlink artifacts. Forty pipelined replay pulls
also prove transfers use the separate quota rather than the 32-query limit.

The actual SDL history workflow watches as an enrolled nonparticipant. It uses
an explicit temporary migration fixture: the current referee recording's
protocol word is changed from 211 to 210, its digest/filename are recomputed and
only its test-database replay identity is updated with the immutability trigger
restored. Tactical bundles and checkpoints remain byte-for-byte unchanged.
Playback reaches the same final tick and state hash, Esc preserves the session,
and complete durable database rows remain unchanged by watching. Dummy audio
checks also require stereo output for the original view, replay and restored view,
with no device-unavailable playback. Removing the
retained file and client cache leaves the history visible and Watch disabled.
The observer's isolated installed-data view begins without a map cache, so the
same workflow also checks the installed-map digest fallback.

Strict MinGW compilation covered the store, client, service and safe file-handle
implementation; native Windows/macOS execution is delegated to their existing
CI jobs, now including the new synthetic history/artifact/replay-loader gates.
The six available x86 GCC/Clang O0/O2/O3 determinism legs retain golden hash
`dcef618cd2e4d558`. The optional local aarch64 legs lacked usable target headers
and were skipped; no ARM execution is claimed.
