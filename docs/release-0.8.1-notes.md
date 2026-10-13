# TAK Engine 0.8.1

0.8.1 follows 0.8.0. It is a client update: the game rules, network protocol and
replay format are unchanged, so **0.8.1 and 0.8.0 players and servers can play
together**. The information bar no longer draws empty boxes and bars. The game
uses less video memory for terrain, and a new **GPU Memory Limit** option sets
how much it may use. The streaming panel can remember your stream key in the
system password store.

- **The information bar shows only what has data.** With nothing selected the
  bar is now plain: the black portrait box and the two pairs of empty bars are
  gone. Every element keeps its place from the original layout (nothing moves),
  but each one draws only when it has something to show:
  - the portrait and the health bar, only with a selection;
  - the mana bar, only for a unit with mana, or a selection that includes some;
  - the name and progress bar on the right, only while the unit is building or
    conjuring something;
  - the right-hand mana bar, which never showed anything, is gone.

  The status text (**STANDBY**, **N SELECTED** and so on) stays centred between
  the two bar positions whether or not the bars draw. In a multi-unit selection
  the first portrait is now part of the strip, the same size as the others when
  the strip shrinks or folds into two rows. A single selection draws exactly as
  before. This is a deliberate departure from the original game's always-present
  layout; see [the user guide](user-guide.md#selection).
- **Less video memory for terrain.** The blurry low-detail terrain that fills in
  while the detailed terrain is still being prepared used to be uploaded whole at
  load and kept for the entire game, duplicating the detailed terrain. It is now
  uploaded only for the parts of the map the view needs, in the same frame, and
  released a few seconds after the detailed terrain covers them. When a zoom
  changes the detail level, the detail already prepared at the old level stands
  in until the new one is ready, so the view never drops to the blurry terrain
  for those frames. Measured with the game's own video-memory count at
  3840 × 2160 on OpenGL (Linux), peak use before and after these changes:

  | Scene (local-server game) | Before | 0.8.1 |
  | --- | --- | --- |
  | Middle Earth V1, camera parked at full zoom-out, about 40 s | 498 MiB | 309 MiB |
  | Middle Earth V1, camera parked at 2.5× zoom, about 40 s | 350 MiB | 121 MiB |
  | Middle Earth V1, two-minute camera sweep with zoom cycling | 721 MiB | 561 MiB |
  | Ulasem Arena, two-minute camera sweep with zoom cycling | 540 MiB | 516 MiB |

  The low-detail terrain alone had held 238 MiB on Middle Earth V1 and 64 MiB on
  Ulasem Arena for the whole game. The count covers the game's own textures and
  buffers, not the screen or what the graphics driver adds.
- **GPU Memory Limit** (Options → Graphics): **Auto** (1.25 GB, the old fixed
  ceiling), 512 MB, 768 MB, 1 GB, 1.5 GB, 2 GB, 3 GB, 4 GB or **Unlimited**,
  applied at once from the title screen or in a game. It is a safety net, not a
  quality setting: each part of the renderer has its own fixed allowance, so
  raising it changes nothing unless the game needs more. Lowering it makes the
  game drop terrain it keeps ready off-screen on the next frame and stop caching
  extra building shading and shadows; the map may look blurry for a moment more
  often after a pan. Units are never hidden: the textures that draw units are
  always created, after asking the terrain caches to give memory back. With the
  limit at 512 MB, a fast 3840 × 2160 camera sweep on Middle Earth V1 peaked at
  511 MiB and showed the blurry terrain somewhere on screen in 0.001% of the
  frames that needed it. The hover help shows how much is in use. See
  [the user guide](user-guide.md#graphics).
- **A graphics driver's out-of-memory error no longer costs quality for the rest
  of the session.** If the driver reports that it has run out of memory (this
  can happen with Direct3D on Windows and Metal on macOS; OpenGL does not report
  it to the game), the game still lowers its limit, but now returns to your
  choice after a minute without another error, waiting longer each time it
  happens again, up to eight minutes. The option shows **LOWERED FOR NOW**
  meanwhile.
- **More of the game's video memory is counted.** Lightning textures, the Darien
  Crusades fallback font and campaign map, and the streaming capture buffers
  (while a stream runs) now go through the same count, so the limit and the
  stats panel's **VID** row see them.
- **Streaming: Remember Key.** The F9 / Settings / Esc streaming panel has a
  **REMEMBER KEY** box under the key field, off by default. Ticked, the stream
  key is kept in the system password store and filled in, still masked, at the
  next launch:
  - **Windows**: Credential Manager (a generic credential named
    `TAK Engine/YouTube stream key`, for your account on this computer);
  - **macOS**: your login keychain (service `TAK Engine`);
  - **Linux**: the Secret Service (KWallet, GNOME Keyring, KeePassXC and others),
    through libsecret loaded when first needed. It is not a package dependency;
    without libsecret or a Secret Service the box is greyed out with
    **NO SYSTEM PASSWORD STORE**.

  Only the on/off choice is saved in the settings file; the key never goes into
  settings or any other file, and there is no file fallback. While ticked, the
  stored key follows the field (saved on tick, paste, Start Streaming and closing
  the panel; an empty field at those moments, or **Ctrl+A**, deletes it), and
  unticking deletes it. If the store refuses a delete, for example when you cancel
  an unlock prompt, the box ticks itself again so it never claims the key is
  gone. Store calls run on a background thread, so a slow password store or an
  unlock prompt never stalls the game. Under Proton or Wine the Windows build uses
  Wine's emulated Credential Manager, which is less secure; see
  [remembering the key](streaming.md#remembering-the-key).
- **Options dropdowns** open upward when the list does not fit below, show whole
  rows only, and draw a scroll bar when the list fits neither way (the nine-choice
  GPU Memory Limit list used to be clipped under the footer). A long label such
  as GPU MEMORY LIMIT is no longer covered by its dropdown.
- **For contributors.** New tests: `gpuvram` (the limit, tightening and relaxing
  arithmetic with a fake clock), `stream_key` (Remember Key through the panel
  against a fake store, including that saved settings never contain the key) and
  `credstore` (a round trip through the real password store under a test-only
  name, skipped where there is none); `terrain_cache`, `selportraits`, `settings`
  and `override_settings` cover the other changes. No raw texture creation is left
  outside the video-memory count in the client. Stale comments about a removed
  whole-frame anti-aliasing target were corrected.

**Compatibility:** protocol **256**, replay format **12**, generator version
**8**, campaign payload **4** and Crusades SQL schema **9**, all unchanged since
0.8.0 (checked in source against `v0.8.0`: every source change is in
`src/client/`, and none touches the simulation, the network code, the server, the
AI, the map generator or the campaign code). A server accepts a client whose
protocol and base game data match, and does not compare engine versions, so
**0.8.0 and 0.8.1 clients and servers can play together** in either direction.
Replays from 0.8.0 play in 0.8.1 and the other way round. Generated-map recipes
and saved `.kmp` maps carry over. Saved settings carry over; the two new settings
(`gpuMemoryLimit`, `streamRememberKey`) are skipped by 0.8.0 if you go back, and
dropped from the file at its next save.

No new required runtime dependencies or retail assets are included: the password
stores use system libraries on Windows and macOS, and Linux loads libsecret only
if it is installed. See the [release validation report](release-0.8.1-validation.md).
