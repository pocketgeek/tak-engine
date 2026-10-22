# TAK Engine 0.8.10

0.8.10 follows 0.8.9. A multiplayer game no longer has to rebuild a dropped
player's world from the start: it waits for them for a short, fixed time, then
hands their seat to a computer player. The YouTube stream can also be saved to
disk, campaign missions always seat you as the original game does, and
Cartographer's Test Map works on macOS. The disconnect rules, the campaign seat
and a retail target-choice detail change what the server and the game
simulation do, so the network protocol moves to **289** (replays stay at format
**14**): **0.8.10 does not play with 0.8.9, 0.8.8 or any earlier version, and
does not play their replays.** Servers and every player must update together.

<!-- LEAD: re-check kNetVersion (289) and kReplayFormat (14) right before tagging. -->

## Disconnects

- **The game waits for a player who stops responding.** When nothing has been
  heard from a player for 8 seconds, or their connection drops without them
  leaving, the game pauses for everyone and every screen shows **WAITING FOR**
  *name* with a countdown. Several missing players each get their own line.
- **30 seconds for the whole game.** Each player has one 30-second allowance for
  the entire game, not one per stop. It counts from the moment the player went
  quiet, so the 8 seconds before the banner are already spent: a first stop
  counts down from **22**. A player who used 20 seconds earlier has 10 left.
- **Short stalls count too.** A player quiet for less than 8 seconds starts no
  countdown, but if the game stands still waiting for them for more than a
  second, the time past that first second comes off their 30 (rounded up to
  whole seconds). A network hiccup of a second or less costs nothing, so
  repeated short silences can no longer hold a game up indefinitely.
- **Loading is free.** Nobody's time is used while the game is still loading,
  however slow a computer is. A connection that closes before the game starts is
  counted down from that moment.
- **Back in time, play goes on.** The game reconnects by itself: a client that
  loses its connection shows **CONNECTION LOST -- RECONNECTING** with the time it
  really has left and tries again every couple of seconds. On return it catches
  up only the moments it missed; nothing is replayed from the start and the
  world is not rebuilt.
- **Out of time, a computer takes over.** When a player's time runs out, a
  **Normal** computer player takes their seat for the rest of the game: the same
  colour, units, buildings and mana. Everyone sees *name* **STOPPED RESPONDING:
  AN AI (NORMAL) TAKES OVER**, and the replay records the takeover, so it plays
  back the same. The player cannot return to that game. A player whose time is
  already used up is taken over at once the next time they hold the game up,
  without a pause.
- **Quitting is leaving.** **Main Menu** or **Quit** from the in-game menu now
  leaves the game at once (your units stand idle), with no wait for the others.
- **Gone:** the 5-minute seat hold, the 2-minute pause allowance and its forfeit,
  and the long rejoin that rebuilt the whole game from its first tick.
  Spectators still catch up that way when they start watching a game in
  progress. In an issued Darien Crusades battle the absent player still forfeits
  when the time runs out, as there is no stand-in in a duel.

See [Disconnects](user-guide.md#disconnects) in the user guide.

## Streaming

- **Save a Local Copy.** A new box on the YouTube Streaming panel, above **Start
  Streaming**, also writes the stream to disk. It is off by default and the game
  remembers your choice (**Defaults** in Options leaves it alone). It is greyed
  out while a stream runs and applies at the next Start.
- The copy is the **same encoded video and audio** that goes to YouTube, so
  nothing is encoded twice: it costs disk writes, not CPU or GPU time. Files go
  to the **videos** folder in your user folder (beside `settings.ini` and your
  replays) as `TAK-YYYYMMDD-HHMMSS.mp4`, one per connection (a reconnect or a
  switch to the CPU encoder starts a new file).
- The file is a **fragmented MP4**, written in two-second fragments, so it still
  plays to the last fragment if the game crashes or the power goes.
- A problem with the copy never stops the stream: the status shows **LOCAL COPY
  FAILED** (folder cannot be created or written) or **LOCAL COPY STOPPED - WRITE
  FAILED** (for example a full disk) and the stream carries on. While saving the
  panel shows **SAVING LOCAL COPY** and the file name, and after **Stop
  Streaming**, **LOCAL COPY SAVED** and its path.

See [Saving a local copy](streaming.md#saving-a-local-copy).

## Campaigns

- **You are always the mission's first player**, as in the original game. In
  *The Iron Plague* missions 22 and 24 (`takx22_mt`, `takx24_mt`), where the
  mission file describes your side as a computer opponent, the server ran an AI
  on your own units and allied you with the Aramon invaders. Both now play as
  intended; no other mission changes.

## Map editor

- **Test Map works on macOS.** Cartographer now finds the game from inside its
  app, whether the two apps came from the disk image or the ZIP, so **Test Map
  (F5)** no longer says it cannot find `takclient`. This known limit of earlier
  releases is gone.

## Gameplay

- **Retail target choice on roads and water.** When a shooter weighs its
  targets, the cost of turning towards one now uses its turn rate scaled by the
  road or water it stands on, as the original game does (the same scaling the
  path search already used). Only shooters on a road or on water are affected.

## Public server

- **Renewal on the CA's schedule.** When the certificate authority offers ACME
  Renewal Information (RFC 9773), the built-in Let's Encrypt renewal renews at a
  point inside the window the CA suggests, and at once when the window has passed
  (how a CA signals an early renewal). Other CAs keep the fixed one-third-of-
  lifetime rule.
- **Revocation checks.** The renewal worker checks the server's certificate
  against its revocation list and renews at once if it is revoked. The game,
  when it connects to a server, and the server, when it talks to the CA, check
  the other side's certificate revocation list and refuse a revoked certificate
  (soft-fail when the list cannot be fetched or used).

See [public server deployment](public-server.md).

## Documentation

- Stale text fixed in the user guide, the Cartographer guide, the campaign
  notes and the retail-engine notes (seat moves, campaign script verbs,
  cross-build play, god toggle, scenarios in skirmish).

## For contributors

- New tests: `server_disconnect` (wait, budget, spent budget, holds, jitter,
  takeover with AI orders, resume, reconnect from the first missing tick, leave)
  and `disconnect_client` (two headless clients: takeover with no desync and a
  replay that verifies, resume, reconnect, reconnect with a login).
  `stream_test` records a local copy and walks its MP4 boxes, with a simulated
  full disk and an unwritable folder; `fire_sweep_turnrate` pins the emulated
  turn rates; `campaign_test` covers `takx22_mt` and a synthetic mission with a
  plain Player2; `tls_test` and `acme_test` gained CRL and RFC 9773 cases, with the
  local fixture `tools/acme_ari_test.py`.
- Protocol 289: `Msg::Waiting` (per-seat countdown plus the recipient's own
  budget), `Rejoin` with its first missing tick, `Reseated`, and
  `Event::AiTakeover`. Removed: the resume-ticket file (`TAK_RESUME`) and the
  debug `--mprejoin`. Test servers shorten the times with `TAK_STALL_MS` and
  `TAK_DROP_WAIT_MS`. `replay_test` refuses protocol-288 replays.
- FFmpeg is now built with the `mp4` muxer on every platform
  (`--enable-muxer=flv,mp4`); the recipe change rebuilds cached prefixes.
- New setting: `streamLocalCopy` in `settings.ini`.
- Retail finding: the target score's turn penalty (`KINGDOMS.icd`
  `0x412a77`..`0x412ae2`) scales `turnrate` by `roadmultiplier` or
  `watermultiplier`, truncated to 16 bits; emulated and ported as
  `retailEffectiveTurnRate`.
- The `--mpai` Inner Circle seed 1 hashes at 60 s are unchanged from 0.8.9:
  Retail `b353410003e98dd5`, Legion `0825aeb9e8139203`. At 300 s:
  also unchanged, Retail `a3d953803a064191`, Legion `ce5c4ba7562e53b9` (no
  shooter in that small game gets a different target score). The remote sweep's
  `h-everything` case, which fields every unit type, does move in both modes
  (it now runs to the time limit), consistent with the turn-rate change; every
  other deterministic sweep case is identical to 0.8.9.

## Known limits

All of 0.8.9's known limits carry over:

- **Legion units still stand and wait more than Retail's**, in crowds, at
  corners and in rings, though they usually arrive sooner. This is cosmetic.
- **Legion attackers through a narrow gate deal more damage than Retail's**:
  in the test layouts from about 1.2 to 2.2 times as much, with more units in
  reach.
- **Power codes go through the game simulation like orders**, so their effects
  are part of every player's game (and of replays). The original applied a
  code only on the computer of the player who typed it. The codes that act on
  the typer (+ATM, +ManaMe, +LOS, +Mapping, +NowISee) still act only for that
  player; +DoubleShot and +HalfShot change everyone's damage.
- **Fog of war: Full Vision does not switch line of sight off at the start.**
  Your units do not attack what they cannot see until **+LOS** or **+NowISee**
  is typed.
- **UI Font: Classic is close to, but not pixel-identical with, 0.8.7's
  interface.**
- **The campaign book's chapter numeral** still touches the bottom of
  "APTER" in CHAPTER, in both UI fonts, as in the original game (its lettering
  boxes overlap).
- **Legion bridges are still a lottery at about 100 units**: two armies meeting
  at a narrow, tapering bridge can jam in its mouth and take far longer than
  Retail to cross.
- A stationary unit choosing its own targets can keep ignoring an enemy it once
  found blocked. The Legion 10,000-unit workloads still struggle with
  congestion and the largest run over the tick budget; in mixed-10000 the
  flyers hover over a slow ground army. Moving the game window between a
  Retina and an ordinary display does not rescale UI Scale. In the remote test
  sweep, Retail `h-cramped` ends early because the idle seat starts with only
  its Monarch.

New with the disconnect wait:

- **A stall on the server's side is charged to the players.** The wait is
  measured on the server's clock, so if the server's own upload stalls for more
  than a second, every player it holds up is charged for it, though none of them
  went quiet.
- **A reconnecting player who hitches while catching up is charged.** If the
  game stands still for more than a second waiting for a player who has just
  reconnected and is still catching up, that time comes off their 30 seconds as
  any other hold does.

## Compatibility

Protocol **289** (was 288), replay format **14**, generator version **9**,
campaign payload **4** and Crusades SQL schema **9**. Checked in source against
`v0.8.9`: `kNetVersion` in `src/net/protocol.h` moved from 288 to 289;
`kReplayFormat` in `src/net/replayhdr.h`, the map generator's `formatVer`
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` (`src/net/crusades.h`)
and `kSchemaVersion` (`src/server/crusades/store.cpp`) are unchanged.

- **Multiplayer.** A server accepts only a client whose protocol equals its own,
  so **0.8.10 cannot play with 0.8.9, 0.8.8 or any earlier client or server**,
  in either direction. Update the server and every player together.
- **Replays.** Playback requires the recording's protocol to equal the engine's,
  so 0.8.10 does not play replays recorded by 0.8.9 or earlier (they still
  write format 14, but under protocol 288), and 0.8.9 does not play 0.8.10's.
  Keep the version that recorded a replay to watch it. Crusades battle replays
  follow the same rule.
- **Maps, mods and settings.** Saved `.kmp` maps, generated-map recipes,
  `mods/` packs, `ModCache/` downloads and 0.8.9 settings files carry over
  unchanged. The one new setting, `streamLocalCopy`, starts off.

No new runtime dependencies or retail assets are included; the bundled static
FFmpeg now also contains its MP4 muxer. See the
[release validation report](release-0.8.10-validation.md).
