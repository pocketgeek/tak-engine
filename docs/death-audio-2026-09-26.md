# Unit death audio — 2026-09-26

## Engine corrections

Death audio now comes only from the authored `Killed`/`Dying` scripts (or the
existing `death` callback when `Dying` is absent). Removed the filename-based
`<unit>die1`/`die2` fallback: a silent script or silent death branch must stay
silent. Frozen/petrified deaths and destroyed transported cargo retain their
existing callback suppression.

The display VM forwards both the sound name index and the authored sound flags
to the main-thread playback queue. The native unit sound host returns zero;
the display VM previously returned the priority instead. Mission sound requests
retain their existing behavior, and authoritative unit-script execution is unchanged.

Retail's low three flag bits select the sound class. Positional classes require
visibility; classes 0/1 additionally require selection and a free voice. Class 7
is global and bypasses positional visibility. Its loop flag is preserved. The
mixer now supports 32 concurrent voices, matching the native manager's slot
capacity; this is separate from mono/stereo/surround output-channel count.
Retail can configure a lower active voice limit; this engine uses the full pool.

When the pool is full, a higher-priority sound can replace a non-looping sound
of strictly lower priority. Choose the lowest eligible priority, then the oldest
start. Equal/higher-priority voices and looping voices remain protected. This
replaces the old unconditional rejection whenever eight sounds were already
playing. Ordinary positional effects default to class 3 and UI sounds to class 7;
unit script sounds use their authored class, including death cries at class 4.
This does not guarantee that every simultaneous death cry will play: retail's
priority policy also deliberately rejects sounds when no eligible voice exists.

## Native evidence

No retail GUI or audio-device launch. Headless `KINGDOMS.icd` observations:

- `50def0`: unit sound flags, visibility/selection/free-slot gates, positional vs
  global routing, and zero return value.
- `50a9c0`/`50a7d0`: named positional lookup and submission.
- `50a720`/`50a6b0`: named global lookup and submission.
- `56ed40`: 32-record voice pool and overflow admission.
- `56ea90`: strictly lower priority, non-looping victim selection, lowest class
  first with oldest-start tie-breaking.

`tools/re/check_death_sound.py` compares 500 randomized voice-replacement cases
and 512 routing cases against those native routines and the production helper.
It also verifies that the real unit host returns zero regardless of the
underlying playback routine's return. It reads only a locally owned retail image.

## Regression coverage and asset limits

`death_sound_test` opens a paused SDL dummy device and drives the actual
production mixer. PCM output confirms that a death cry replaces lower-priority
voices, while equal/higher priorities remain protected. Looping playback wraps
and is protected from eviction. Synthetic COB checks verify delivery of the
name/flags and the host's zero return in both display interpreters.

The data-backed test exercises eight death types across all 202 loaded unit
scripts in both balance modes, following the renderer's owner-stop rules. It
also decodes every named death-cry variant, including random branches not taken
by that particular timeline, plus sounds actually emitted during death.

Two script references have no matching WAV in the installed retail data:
`cregatldie1` (Gatling Crossbow) and `zonbardie1` (Giant Barracuda). These remain
silent on failed lookup, as retail does. No unrelated cry is substituted and no
game assets are added. If a data override supplies either file, the normal lookup
will play it and the roster test will decode it.

## Validation results

- All targets rebuilt in Release, optimized Debug and Debug.
- All 180 CTests passed: 58 Release, 61 optimized Debug, 61 Debug. Audio tests
  were also rerun after adding portable SDL startup for platforms using
  `SDL_MAIN_HANDLED`.
- Both balance rosters exercised 202 scripts and emitted 235 death-sound events
  each. All 101 present referenced death WAVs decoded; the two known absent
  shipped references remain silent.
- Native comparisons: 500 voice-selection cases and 512 unit sound-host cases,
  all matching.
- Deterministic math passed the guard and available GCC/Clang optimization-level
  checks, golden `dcef618cd2e4d558`. ARM cross-builds were skipped after their
  test builds failed in the local harness.
- Two local multiplayer runs (Release server, optimized Debug client) both
  retained the pre-change state hash `3c4e5e85a939988c`.

## Positional volume comparison — 2026-10-03

Read-only native emulation of `KINGDOMS.icd`, SHA-256
`1144a394889811ae6d113f8fe470dac5de0b0a7aae3c9ee063f74b8d20b730db`,
followed the positional routine `0x50a7d0` (file offset `0x10a7d0`) through the
real voice manager `0x56ed40` to its Miles audio imports. Map-cell lookup was
controlled; sound-enabled state, camera rectangle and visibility planes were
synthetic. The Miles calls were intercepted rather than opening an audio device.
The temporary probe is `/tmp/tak-retail-sound-distance.py`; no retail assets were
copied or modified.

The routine applies a **two-level viewport volume**, not continuous radial
falloff. Inclusive camera-rectangle bounds use camera origin at game offsets
`0x14ed0/0x14ed4` and extents `16 * 0x19ea8/0x19eac`. An admitted source inside
that rectangle gets volume **127**; a source outside gets **64**, regardless of
further distance. The rectangle compares raw integer X/Z; height affects the
visibility lookup (`Z - floor(Y/2)`) but not this volume test. Visibility and
valid-map-cell gates can suppress playback completely.

With a synthetic 640×480 viewport starting at (1000,1000), the native Miles
`AIL_quick_set_volume` call receives:

| Source | Volume | Pan |
| --- | ---: | ---: |
| Center (1320,1240) | 127 | 64 |
| Left edge (1000,1240) | 127 | 32 |
| Right edge (1640,1240) | 127 | 96 |
| One pixel right of view (1641,1240) | 64 | 96 |
| Far right (10000,1240) | 64 | 127 |
| Far below (1320,10000) | 64 | 64 |

Pan is clamped to 0..127. Before clamping it is 64 plus the signed,
truncated-toward-zero quotient of `(sourceX - cameraX - 16*floor(widthCells/2))*64`
by `16*widthCells`. Thus retail has milder panning at the viewport edges than
our current full-left/full-right edge panning. Priority 0..6 passed to this
positional routine does not change the volume; the upstream unit host separately
applies selection/visibility gates. The global class-7 path bypasses this routine.

Validation: 1,000 randomized source positions agreed with the recovered
viewport-volume/pan formula, plus explicit edge, offscreen, height and
hidden-source cases. All positional priorities produced the same offscreen
volume. The native manager passed these values unchanged to
`_AIL_quick_set_volume@12` through IAT slot `0x5eb494`. This confirms the API
settings, not an exact acoustic loudness or dB ratio after Miles/driver mixing.

Engine before the falloff change below: `Sounds::playWorld` and `repan` computed pan/depth but
applied no camera-distance volume factor; the sample gain stayed 1. Consequently,
our offscreen sounds lacked retail's 127→64 reduction. Retail also computes these
values at sound submission; its voice record retains no emission coordinates,
whereas our mixer re-pans ongoing positional sounds as the camera moves. These
findings do not change engine playback in this comparison task.

The Beast Handler (`zonhand`, whip-bearing builder) is not a special global or
boosted sound case: its build callback requests `STSWISH0` with flags 4, and its
attack requests `SWSWISH9` with flags 4. Aramon Swordsmen also use `STSWISH0` with
flags 4; the Beast Lord uses `SWSWISH4` with flags 4. The earlier routing probe
still passes all 512 native unit-host cases and 500 voice-replacement cases.

## Intentional progressive offscreen falloff — 2026-10-03

Implemented after the comparison at the user's request. Positional sound gain
is 1 inside the camera rectangle. Outside it, let `d` be the shortest Euclidean
distance to the rectangle and `L` the shorter full viewport dimension, both in
world coordinates. The added gain is `max(0, 1 - d/L)^2`: continuous at the view
edge, quarter amplitude at `L/2`, and silent at or beyond `L`. Corner distance
uses both axes. This replaces neither the existing panning nor authored gains.

`playAt` sets the gain at admission; listener updates and `repositionWorld`
recompute it for ongoing sounds. It multiplies PCM before both speaker panning
and the LFE feed, so the same attenuation reaches all outputs and streaming.
Fully inaudible new one-shots skip sample loading and voice admission; looping
sources keep advancing silently and can become audible when the camera returns.
Nonpositional UI/global sounds and background music retain their existing gain.

`death_sound_test` checks actual PCM output at the boundary, successive offscreen
distances, all four edges and a corner, camera movement, zoom, inaudible voice
admission, a silent loop becoming audible, and nonpositional sound immunity.

## The simulation's death cry — 2026-10-14

Retail draws unit-script RAND from the one shared game stream, so the cry a dying
unit's `Killed`/`Dying` picks is part of the game every peer runs. The client
used to roll its own on the display VM, first from one seed shared by every VM,
then from a per-unit seed. Either way two peers, a replay and a spectator could
each hear a different cry, none of them necessarily the simulation's.

The simulation now keeps every PLAY_SOUND made by a dying unit's VM (`Killed`,
`Dying` and any thread still running in it) as `World::ScriptSound`: tick, unit,
owner, position, name index and flags. Like the script emissions it is
presentation only: not hashed, no RNG, cleared every tick, and the host still
returns zero. The client captures the records after each tick, resolves the
name, and plays each one when the displayed tick reaches it, through the same
`50def0` gate as before: class 7 is global, classes 2-6 need the death position
in the local player's sight, 0/1 also need the unit selected. Once a display VM's
unit is dying it drops its own sounds, so nothing plays twice; its other sounds,
death animation and effects are unchanged, and the per-unit seed stays for
display-only rolls (idle loops, smoke). Frozen/petrified deaths, destroyed cargo
and consumed lodestones run no death script in the simulation either, and stay
silent. A cry released more than 15 ticks after its own tick (a rejoin or
spectator catch-up, fast replay playback) is dropped instead of joining a burst.

`creon_death_sound` kills 1,152 bodies (every Creon `Killed`/`Dying` caller plus
two archers, normal and explosion deaths, own and enemy) through the combat
damage path. Every death must request exactly the simulation's sounds in order,
never two cries, at the authored rates and variants; enemy deaths out of sight
are silent, and the queued cry replayed with every class shows only class 7
carrying out of sight. A death the presentation reaches 10 ticks late still
cries; 45 ticks late it does not. Petrified and frozen victims stand as statues
in silence. `creon_death_sound_peers` (about 9 s) runs the game twice, the
second time presenting every batch late as after a rejoin, and requires the same
world hash and the same sounds for all 1,152 deaths. Before this change, with the
same instruments: 340 deaths sounded something other than the simulation's
choice, 207 deaths differed between the two peers (`crebeas`, `cretort`,
`crefire`, `cregatl` and `cregod`: the units whose idle loops roll RAND on the
display VM before the hit), and the 45-tick backlog cried 16 of 16.
Leaving the display VM's sounds on as well doubled 541 deaths.

No hashed state changed and no protocol bump: --mpai Inner Circle seed 1 Retail
300 s `95b05c8290b55317`, 60 s `84fdef37bbb3606e`, Legion 60 s
`55b0eec316e45071`; detmath golden `dcef618cd2e4d558`.
