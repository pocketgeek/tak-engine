# Menu Bink colors, September 20, 2026

The Credits hover video used orange/brown gold against the yellow/gold MainBG.
The earlier idle-only workaround concealed the rectangle at rest, but did not
correct playback. Its comments incorrectly attributed the color loss to Bink
compression based on comparisons of neutral pixels only.

An independent Win32 probe loaded the user's shipped `binkw32.dll` under Proton
10.0, decoded the actual clips, and copied BGRX output through its exported API.
No retail implementation or assets are copied into the engine. Plain system Wine
hung during prefix initialization on this machine; Proton worked.

Against FFmpeg's decoded Y/Cb/Cr planes, the reference output follows approximately:

```
R = 1.164(Y - 16) + 2.017(Cr - 128)
G = 1.164(Y - 16) - 0.813(Cb - 128) - 0.392(Cr - 128)
B = 1.164(Y - 16) + 1.596(Cb - 128)
```

This exchanges the normal BT.601 chroma coefficients. Limited-range luma remains
correct. Neutral-only comparisons could not expose the error. `BinkVideo` now
explicitly configures swscale with this matrix; nearest chroma sampling and the
odd-width safety handling remain intact. This shared path covers Bink playback,
not just the Credits door. The main-menu idle rendering policy is unchanged;
its misleading explanation has been removed.

Validation uses `tools/re/check_bink_colors.py`, which compiles a small original
API probe with Zig and runs it in its own temporary Proton prefix. It compares
idle, hover-start, hover frame 10, and mouse-out frames for all four doors against
`biktool` using the engine decoder. The reference copies every frame because the
old DLL updates dirty regions. Comparing a late frame into a fresh blank buffer
would falsely report large errors.

Example invocation:

```
python tools/re/check_bink_colors.py --data assets/game \
  --proton "$HOME/.local/share/Steam/steamapps/common/Proton 10.0/proton" \
  --steam "$HOME/.local/share/Steam" --biktool build/biktool
```

The test allows mean RGB error below 3/255 for integer/chroma rounding differences;
this is close color agreement, not a claim of byte-identical decoding. The old
conversion had mean errors of 6.713 on SNORT4, 5.511 on GIRL4 and 4.071 on KNIGHT4.
All three local client builds were rebuilt. Simulation/pathfinding are untouched.

All 16 reference comparisons passed: corrected mean RGB error ranged from
1.151 to 2.058/255. The focused retail-visual and shadow CTests also passed.

## Full door clip frame and cadence trace, September 25, 2026

`tools/re/check_bink_all_frames.py` extends the sampled-color check across every
frame of all 16 clips (`machine`, `girl`, `knight`, and `snort`, clips 4–7). Its
headless Win32 helper loads the shipped Bink DLL under Proton and follows the
native playback calls: retail's `BinkAnimButton` path at `0x423c60` waits on
`BinkWait` before `BinkDoFrame`, copies the decoded surface, then advances with
`BinkNextFrame`. The helper records each frame's QPC time and BGRX pixels; no
retail executable or GUI is launched. It cross-checks dimensions, frame count,
and rate from the BIK container header against `biktool`, then compares every
native frame with the engine's `BinkVideo` output.

All 348 frames pass with worst per-frame mean RGB error of 2.086/255 (the
existing bound is 3/255). The multiframe clips report a median `BinkWait`
interval of 33.00 ms against the BIK header's 33.33 ms at 30 fps. The idle
clips are single-frame streams, so they have no interframe cadence to measure.
This verifies the complete decoded image sequence and native Bink frame clock.

Reproduce with:

```sh
python3 tools/re/check_bink_all_frames.py --data assets/game \
  --proton "$HOME/.local/share/Steam/steamapps/common/Proton 10.0/proton" \
  --steam "$HOME/.local/share/Steam" --biktool build-o2/biktool
```

## Door hotspot and state trace, September 25, 2026

Retail creates each door with a custom hit rectangle inside the broader GUI
gadget rectangle: Machine `(71,219,101,158)`, Girl `(289,217,62,168)`, Knight
`(487,216,63,157)`, and Credits/Snort `(124,42,71,130)`. Its hit-test at
`0x4ad170` uses inclusive right and bottom edges and uses that custom rectangle
whenever both dimensions are set. TAK now uses those same rectangles for hover
and click selection; the GUI gadget rectangles still place the video art.

The native menu update waits for clip 5 to finish after the pointer leaves. It
then enters clip 6, and the next update starts clip 7 when focus is gone. If the
pointer returns during clip 7, clip 7 continues to completion; hover starts clip
5 again once the object is idle. The local door update follows those transitions.
The click method writes native states 3 then 4 before invoking the menu callback.

`tools/re/probe_menu_door_native_state.py` headlessly executes the retail
hit-test, hover-enter, update, and click methods for all four synthetic button
objects in Unicorn. It verifies the hotspot boundaries, leaving during clip 5,
re-entering during clip 7, and click-state mutation. Bink frame records are
synthetic; BinkGoto is recorded through a stub, while audio and the menu callback
are disabled. The focused `door_animation_test` exercises the same hotspot and
state-transition helpers used by the local menu. Neither check launches the
retail GUI or claims to test its outer SDL/Windows input dispatch.
