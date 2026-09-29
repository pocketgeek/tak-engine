# Unit Info dialog

Retail asset check: `data.hpi!guis/unitinfo.gui` (2026-09-28).

- The title is **Unit Info**; the labels are Max Velocity, Acceleration, and
  Turn Rate. The title and labels explicitly select `lombardic (cd).gaf`.
- The OK gadget references `genericdialogue.gaf/OkButtons`, frames 0–2,
  and `ok.wav`. The engine uses the normal and hover frames, like its other
  retail menu buttons, and activates on a left-button press.
- The root binds both Enter and Escape to OK.

The engine previously drew only the idle button, omitted its click sound,
used the HUD statistics font for every label, and allowed dialog clicks to
reach world input. The dialog now owns input while open; the simulation
continues running. F1 (or the rebound Unit Info key) also closes it.

Numeric fields have no explicit font in the shipped dialog. We use the
retail Times New Roman body font with dark text for contrast on parchment.
That contrast choice is a readability adjustment, not a claim of pixel-exact
retail rendering. Font proportions are preserved and text only shrinks when
needed to fit its field.

Validation: both client builds; SDL rendering and event checks for normal/
hover artwork, left-only activation, modal click handling, and Enter/Escape/
F1 dismissal. Comparison used the shipped dialog/assets, not a live retail
capture.
