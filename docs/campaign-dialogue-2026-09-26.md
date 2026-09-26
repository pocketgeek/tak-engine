# Campaign dialogue and audio — 2026-09-26

Campaign sounds now preserve each authored request instead of retaining only the
last sound name in a snapshot. Two script calls in one tick and render frames
that skip simulation snapshots no longer discard earlier requests. Sound events
cross the simulation/render boundary in order, with their mission tick and sound
flags captured before the renderer consumes them. Each queue is bounded to 256
requests, including headless worlds that never consume cosmetic audio. Audio
consumption is excluded from authoritative state and does not change its hash.

The mission host now preserves the script's sound priority. Previously the
callback discarded its flags and the renderer submitted every line as class 7.
Retail mission host `4d3580` takes the low three bits, requires a free voice for
classes 0/1, and submits a global, non-looping sound at volume 127. It returns
zero. Higher flag bits do not enable looping in this host. This differs from the
unit sound host's visibility/selection/loop routing. Our existing mixer enforces
the matching free-voice/priority admission policy; missing WAVs remain silent.

## What the dialogue assets actually are

`guis/playercampaigndialogue.gui` is the campaign **player/profile chooser**. It
contains `PlayerList`, described as “List of user names,” with OK/Cancel and
scroll controls. Its name is not evidence for an in-mission character portrait
widget. Earlier campaign-design notes inferred that widget incorrectly.

Actual on-map authored sounds are COB `PLAY_SOUND` events. Extracted scripts
include mission-specific clips in chapters 9, 31 and 33; Monsara lines in
chapters 38/39/39a; two creature lines in chapter 47; and weapon/death sounds in
Iron Plague chapter 3. These calls belong at their authored callback timing,
including delayed or repeated story events. A matching `<mission>.wav` filename
alone does not mean it should play when the briefing opens.

Chapter movies carry their own audio. Initial briefing behavior is documented
separately by the campaign presentation work; no new dialogue text, substitute
clips, portrait overlay or automatic briefing voice is inferred from filenames.

## Evidence and tests

No retail GUI was launched. `tools/re/check_campaign_sound.py` executes native
`4d3580` across 512 flag/free-voice cases, captures its global playback arguments,
and checks priority/admission against the production helper. It also verifies
the native zero return despite a nonzero controlled playback result.

`campaign_audio_test` runs a synthetic mission through the real MissionScript
and COB VM, verifies ordered names/flags from two same-tick sound requests,
consumption exactly once, unchanged simulation hash after draining, and bounded
headless storage. Its data-backed mode scans mounted mission COB references and
decodes every present WAV. Missing installed clips are listed explicitly; it
does not silently substitute other sounds or claim absent assets are playable.

Focused optimized Debug validation passed, including all 512 native routing
cases. Mounted data contains eight mission scripts referencing eight distinct
clips: five decoded successfully. Three referenced WAVs are absent:
`arajoefire.wav`, `seaczon1.wav`, and `undetar6.wav`. Their scripted timing remains
intact, and normal lookup will play them if an override supplies the files. No
unrelated clip is substituted. The synthetic regression also confirms that
`Start` is queued until the first mission-script tick, so opening the paused
initial briefing does not execute its script audio early.

## Objective completion cue

Ten non-timer victory condition classes request the named `Victory Condition`
sound at global priority 7, non-looping, volume 127. Defeat rules and the shared
victory/death timer predicate do not request it. Added this authored request at
death/capture event completion and polling completion, with an independent
per-condition `soundPlayed` flag matching native offset `+8`. This flag cannot be
replaced with the completion bit: `DestroyAllUnits` can become false again after
an enemy appears, yet native never repeats its cue. The flag affects presentation
only and is excluded from the deterministic state hash.

Native sound-call sites are `5236d7`, `523790`, `52390b`, `5239ed`, `523b61`,
`523cc3`, `523db5`, `523f62`, `524064`, and `524164`; timer `524270` has no call.
The native audio oracle additionally exercises repeated commander-death events,
false/true destroy-all transitions and timer boundaries. Engine regressions
check immediate event delivery, no poll/event duplication, independent cue
latching, and silent timers. No matching `Victory Condition.wav` appears in the
extracted installed sound data; normal failed lookup remains silent, and a
matching override can supply the authored clip without changing code.
