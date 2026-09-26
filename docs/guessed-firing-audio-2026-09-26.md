# Authored firing audio — 2026-09-26

Removed the renderer's generic weapon-kind sound selection: melee impact clips,
Drake fire, lightning and bow sounds are no longer substituted for a silent
script. Firing audio comes from the authored attack/weapon callbacks, preserving
their sound name, flags, timing and deliberately silent branches. Impact sound
classes continue to run separately when a weapon hits.

The old `cobSounds`/`hasSounds` switch scanned the entire script for a PLAY_SOUND
opcode, including unrelated selection/death audio. It did not establish whether
a particular attack should make a sound. That scan and its cached flags are now
removed. No authoritative simulation or script RNG behavior changes.

## Retail evidence

`tools/re/check_weapon_fire.py` executes the locally installed retail image
without launching its GUI. The existing 4,132 native `530140` cases verify
FireWeapon dispatch, reload/event updates and callback slot identity. Explicit
traps on named and resolved global/positional audio routes now verify that a
silent script callback does not acquire an engine-invented firing sound.

The same test also executes display receiver `4ea640` for all 256 possible slot
bytes. Each dispatches exactly one authored FireWeapon callback and no additional
audio. Sender `4ea560` constructs the display callback packet; it does not choose
weapon-kind audio. These observations cover firing dispatch, not all possible
sound-producing effect initializers.

## Regression coverage

`firing_sound_test` drives every loaded unit's weapon slots in both balance modes,
including activation, flight, aiming and repeated firing. It compares the exact
sound name-index/flags/tick stream between the display VM (two render frames per
simulation tick) and the integer retail script VM. It decodes each observed WAV
and retains silent timelines instead of fabricating audio for them. This checks
a deterministic callback scenario; it does not claim to enumerate every random
branch or every possible combat circumstance.

Optimized Debug validation passed: 198 standard and 199 Crusades weapon-slot
scenarios produced 376 and 378 authored audio events respectively, with 27 silent
timelines in each balance. All event streams matched. Eighty-two distinct WAVs
decoded. The installed data lacks `archer1.wav`, `archer2.wav` and `archer3.wav`,
referenced in these timelines by Taros Archer, Taros Mage and Aramon Archer.
These failed lookups remain silent rather than substituting unrelated clips;
the test allows only these explicitly identified missing references and will
decode them if a data override supplies them. Native firing/display checks also
passed (4,132 and 256 cases).

## Legacy engine-authored start sounds

Retail also has a separate authored engine path: the weapon loader reads
`soundstart` into weapon offset `0xac` (`5316f2`) and `soundtrigger` into flag
`0x200` (`5310d5`). The common burst/projectile-copy path at `52b0d0` uses that
flag to submit the resolved start sound at priority 4. Thus “FireWeapon dispatch
does not invent audio” must not be read as “the engine can never play weapon
start audio.” These are explicit authored fields, unlike the removed guesses.

Neither field appears in the extracted shipped definitions. The roster test
also reports their occurrence in mounted standard/Crusades FBI definitions,
so mounted assets and overrides are checked rather than assuming the extracted
copy matches runtime data. Support for a mod that introduces these dormant
legacy fields is outside this shipped-unit correction; no such sound is
replaced by a guessed bow/fire/lightning clip.

Mounted validation confirmed 400 unit definitions with zero `soundstart` and zero
`soundtrigger` references. The corrected heading/pitch/slot callback scenarios
also passed on the rebuilt optimized Debug test binary.
