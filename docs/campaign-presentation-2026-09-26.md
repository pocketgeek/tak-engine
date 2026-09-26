# Campaign presentation against retail

This pass compares the current engine to installed assets and isolated native
routines. No retail GUI is launched. The existing victory/defeat screen already
used authored plates; the old campaign design document's blanket claim of block
font result screens was stale.

## Book and chapter selection

Native `4a89c0` loads `guis/BOD.gui`. Its constructor expands the `ChapterImage`
widget's short image-state list to the entire `Story1` GAF sequence. Native
`4a9640` selects the one-based chapter frame for Book of Darien; Iron Plague and
IPalt add 49. Unknown campaign names select frame 49; a frame beyond the sequence
falls back to frame 0. `probe_campaign_chapter_art.py` executes these branches and
passes 112 campaign/index/frame-count combinations.

The campaign picker now uses that book plate, authored previous/next/play/back
buttons, `bodfontbody` and `bodfontdecor`, chapter title and illustration. It
supports chapter paging, Enter to play and Escape to return. Campaign tabs and
all chapters remaining playable are retained product behavior; this change does
not introduce retail's profile unlock restrictions. Completed chapters influence
the suggested opening page and remain replayable. The alternative finale stays
accessible on the last page of the Iron Plague campaign.

The loaded `Story1` sequence contains 75 frames. The original GUI state list has
only 15 entries; using that list directly would show the wrong art for most of the
campaign, hence the native constructor comparison.

## Initial briefing

Native `4b3360` constructs the in-game UI. At `4b34be`, campaign mode creates
`4b51e0`, which loads `Briefing.gui`, calls the pause helper, fills its chapter,
title and objective lines, then registers it as an overlay. Its close handler
`4b53e0` clears the pause flag. This is a pause over the **loaded game**, not a
separate pre-loading BEGIN/BACK screen.

The briefing renderer uses the shipped 510×339 layout and named Line0/Line1/Line2+
text rectangles and fonts over the caller's captured game image. Dismissal resumes
play; it has no invented BEGIN/BACK buttons. Long objective sets can scroll so
content is not lost. The startup flow places this modal behind the existing
initial loaded-player barrier and services connection keepalive while it is open.

The old unconditional `Sounds/<stem>.wav` playback was removed. The native
briefing constructor makes no such request, and some matching clips are authored
for later COB events (for example mission 09); playing them at briefing was early
and could duplicate script narration. Intro movie audio and mission `PLAY_SOUND`
remain responsible for their authored lines. `PlayerCampaignDialogue.gui` is a
profile chooser, not a character portrait dialogue widget.

## Result score

The previously displayed `built*2 + kills*10 - losses*3` score was invented.
Native result draw `4b47ab` reads an accumulated player score. Native death logic
`512bd5..512c6b` adds the victim type's `experiencepoints` (`UnitType+0x1c2`), with
32-bit arithmetic; `4c0016` loads that field with default 666. It does not award
same-owner kills, and does not reject a different owner just because it is allied.
The `probe_campaign_score.py` slice passes 216 combinations of ownership, missing
attacker sentinel, unfinished-construction gate, disabled-score flag and integer wrap.

The engine loads authored experience points and accumulates a derived player
score once at the existing death-accounting point, then snapshots it for the
results screen. Building and losing units no longer manufacture points. This
counter does not change combat or veterancy. Native mission SET40 (`4d3ffe`)
writes player0 score and sets game+3070 bit4 (`4d4011`), disabling automatic
scoring globally. GET40 (`4d42f3`) reads the requested zero-based player slot.
The previous elapsed-tick approximation is removed. Campaign hashes include
scores, the disable latch and pending kill attribution because scripts can read
these values; ordinary skirmish hashes retain their presentation-only behavior.
The native probe also executes both complete host functions across all eight
player slots, signed values and preexisting flag combinations. A real mission
COB regression verifies GET/SET40, disabled accrual and replay reset. The
extracted shipped mission roster has one direct GET40 use: `takx03_ph`, word2279,
with player0 and a 2500-point comparison. That story beat is score-gated, not
elapsed-time-gated. No shipped nonzero score-player selector was found.

## Validation

`campaign_presentation_test` covers same-owner/enemy/allied victim scores and
single-award death accounting and unfinished-unit exclusion. With retail data it also checks both balance
registries, authored book/font/chapter texture loading, chapter navigation and
selection, alternative-finale access, and complete texture teardown. Its optional
output prefix writes base and Iron Plague screenshots for visual inspection.

Native probes passed independently. Coordinated build, CTest, loaded-briefing
and screenshot checks are summarized in the
[integration validation](campaign-design.md#integration-validation). The implementation aims at accurate retail behavior and authored
presentation, without claiming a pixel-for-pixel retail frame comparison.

Additional native result-plate check: `4b425d..4b429b` selects the local player's
faction descriptor and formats `Victory%s.gui`; it does **not** use the map's
terrain kingdom. The existing faction-based result-plate choice is retained.

Screenshot review caught the book background's reserved button rectangles and
buttons whose authored image dimensions exceed their GUI hit rectangles. The
book now composites resting button art at source resolution before filtering and
draws each button at its native image dimensions, avoiding black borders/holes.
The unimplemented profile/load controls are visibly disabled. Serif text uses the
installed palette-bearing `b_times new roman` variants, as existing load/result
screens do; the unprefixed GAF names lack their sibling palettes in this install.

Score ownership is captured at impact, rather than looked up from the killer's
possibly changed owner during later death accounting. The regression changes the
killer's owner between these events and verifies points remain with the original
player. Capture/self-destruction clears the attribution alongside its existing
last-attacker reset.

Focused validation completed: optimized-debug `takclient` and
`campaign_presentation_test` rebuilt; the data-backed presentation test passed.
Both 960×720 Book of Darien and Iron Plague captures were inspected after correcting
the button reserves/font loading. Assets, proportions, chapter titles and
illustrations are coherent. Picker Play/Back route the authored GUI WAV through
the main menu's persistent click device, with its existing flush before launch;
the test checks Play requests `ok.wav`. Result-screen click audio remains a
separate existing frontend limitation; no replacement sound is invented here.
