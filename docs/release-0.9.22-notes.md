# TAK Engine 0.9.22

0.9.22 follows 0.9.21 the same day. It changes who hears and reads you in a game, adds
two voice chat controls, and makes the game's use of video memory steadier.

**Compatibility.** 0.9.22 uses network protocol **316** (0.9.21 used 315) and replay
format **15**. **0.9.22 does not play with 0.9.21 or any earlier version, and it does not
play their replays.** A server and all of its players update together: a 0.9.21 server
turns away 0.9.22 players, and a 0.9.22 server turns away 0.9.21 players, with a
*protocol version mismatch* message. The game itself plays exactly as in 0.9.21, in
Retail and Legion pathfinding alike: the protocol changed only for chat and voice. The
random map generator (version 9) is unchanged.

## Chat, voice and spectators

- **Your allies only, to start.** Each game, the Diplomacy screen's **Chat** and **Voice**
  columns now start ticked for your team (you included) instead of everyone. Tick other
  players to reach them too.
- **A Spectators line.** Under the players, the Diplomacy screen has a **Spectators** line
  with its own **Chat** and **Voice** boxes, both off at the start of each game. Tick
  them to let anyone watching read your chat or hear you. Before, spectators got your
  chat and voice only while every player was ticked.
- **Spectators listen; they do not talk.** Once a game is running, a spectator cannot
  chat or use voice chat: the chat box does not open, push to talk and voice activation
  send nothing, and the server drops anything a spectator sends anyway. Spectators still
  read and hear the players who tick them in. Before the game starts, everyone in the
  room chats and talks to everyone, as before.
- Spectators still need a game's password to watch it. A game has no spectator limit of
  its own; a server takes up to 256 connections in all.

## Voice chat

- **Microphone** (Voice Chat page, on by default): off turns your microphone off. Nothing
  is sent, by push to talk or voice activation, the system's microphone-in-use light goes
  out, and you still hear the other players. Holding push to talk with it off shows
  **MIC OFF** where **YOU** would appear.
- **Mic gain** replaces Mic volume: 0% to 400%, where 100% sends the microphone as it
  records and higher boosts a quiet one. A saved Mic volume keeps its meaning. Keep the
  Mic level meter out of the very top while you speak, or your voice distorts.

## Video memory

- **Reused, not reallocated.** The terrain images, the stand-in map pieces shown while
  they are prepared and the shadow pages now keep the video memory they release and fill
  it again for the next image of the same size, instead of freeing and allocating it as
  you pan and zoom. On a 60-second Benchmark camera tour at 1920x1080, no terrain image
  is thrown away any more (85 of 270 were, and were prepared again when the camera came
  back), and 122 stand-in pieces reused memory.
- **The terrain cache grows with GPU Memory Limit**: 40% of the limit, from 256 MB to
  2 GB (2 GB with Unlimited). At the 1.25 GB default it is 512 MB; at 4 GB, 1.6 GB, so a
  higher limit keeps more of a big map ready and prepares less of it again.

## Under the hood

- Protocol 316: chat carries a trailing spectators byte and voice a spectators flag; in a
  running game the server sends to the ticked players and, with the flag, to the
  spectators, and drops a spectator's own chat and voice.
- Render telemetry: `terrain_slot_creates`, `terrain_slot_reuses`,
  `terrain_slot_pooled_bytes`, `terrain_section_reuses`, `terrain_section_pooled_bytes`,
  `shadow_page_reuses`, `shadow_spare_page_bytes`.
- The `--mpai` gates keep the 0.9.21 hashes; see the
  [validation report](release-0.9.22-validation.md).
