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
