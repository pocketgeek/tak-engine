# TAK Engine 0.9.17

0.9.17 follows 0.9.16. It tidies the settings screens: more of Options uses dropdowns,
every settings screen puts Back in the same place, and the YouTube Streaming,
Settings, Benchmark and Load Replay windows now grow with the window like Options.

**Compatibility.** Everything here is interface only. 0.9.17 keeps network protocol
**309** and replay format **15**, so **0.9.17 and 0.9.16 play together in either
direction, and each plays the other's replays.** A server needs no update. Older
versions stay refused: 0.9.15 and earlier do not play with 0.9.17, and their replays
are refused.

## Options

- **Texture Filtering**, **Hidden Units**, **Health Bars**, **Build Menu** and **Chat
  Position** are dropdowns like **Renderer**, with the same choices as before (Off /
  Units / Everything, Off / Dots / Fade, Off / Damaged / Always, Left / Center / Right,
  Left / Centered).
- **UI Font** is now **Modern UI Font**, an on/off switch. On is the modern face (the
  default), off is the classic one. It is saved as before (`uiFont = modern` or
  `classic`), so your current choice carries over.
- The **Voice Chat** row in the Audio section is gone. Open the Voice Chat page from
  **Settings** on the main menu, or from the game menu in a game.

## Back and Defaults

On Options, Voice Chat and Controls, **Defaults** now sits at the left edge of the
footer and **Back** at the right, in line with the rows; they used to sit together in
the middle. Settings, Benchmark and Load Replay already had Back on the right, and their
"ESC - BACK" hints are gone. Esc still goes back everywhere.

## YouTube Streaming

The streaming window has a **Back** button at the bottom right, and is laid out in the
Options screen's size and text: the same width, a large centred title, and rows that
grow with the window. Errors, the upload warning and notices such as a lowered bitrate
show beside Back, made smaller when a long one would not fit. The "ESC / F9: CLOSE"
hint is gone; Esc and your streaming key still close the window, and the stream keeps
running until you stop it. A long stream key shows at most 24 asterisks.

## Window size

Settings, Benchmark, Load Replay and YouTube Streaming used to stay the same size in
pixels at every window size, so on a large screen they looked small beside Options.
They now use the Options scale: 1x up to 1280 × 720, growing to 3x at 3840 × 2160 and
beyond, and shrinking only on a window too small to hold them.

## Known limits

- The new layouts were checked in screenshots at 1024 × 768 and 1920 × 1080 with the
  modern font, not at 4K, on a high-DPI display, or with the classic font.
- The streaming window was not seen with a long error, nor during a live stream.
- All of 0.9.16's limits carry over (see
  [release-0.9.16-notes.md](release-0.9.16-notes.md#known-limits)).

Validation: [release-0.9.17-validation.md](release-0.9.17-validation.md).
