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
