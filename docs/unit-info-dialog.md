# Unit Info dialog

Retail asset reference: `data.hpi!guis/unitinfo.gui`.

- The title is **Unit Info**; the labels are Max Velocity, Acceleration, and
  Turn Rate. The title and labels explicitly select `lombardic (cd).gaf`.
- The OK gadget references `genericdialogue.gaf/OkButtons`, frames 0–2,
  and `ok.wav`. The engine uses the normal and hover frames, like its other
  retail menu buttons, and activates on a left-button press.
- The root binds both Enter and Escape to OK.

The OK button draws its normal and hover frames and plays `ok.wav`; the title
and labels use the Lombardic font. The dialog owns input while open, so its
clicks never reach the world; the simulation keeps running. F1 (or the rebound
Unit Info key) also closes it.

Numeric fields have no explicit font in the shipped dialog. We use the
retail Times New Roman body font with dark text for contrast on parchment.
That contrast choice is a readability adjustment, not a claim of pixel-exact
retail rendering. Font proportions are preserved and text only shrinks when
needed to fit its field.

The comparison is against the shipped dialog and assets, not a live retail
capture.
