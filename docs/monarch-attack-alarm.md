# Monarch attack alarm

The monarch warning is **AlarmMon**, not the faction's general
`underattack_sound` (normally AlarmAra). In KINGDOMS.icd, the local-owner branch
at `409f7a..409fb2` recognizes the commander flag and calls `50abf0` without the
visibility check used for the general unit alarm. Same-owner damage is excluded
unless the hit's damage type is 1. The caller also excludes its special order
flag `0x80`; the corresponding rare scripted-order behavior has not been mapped
for this sound.

`50abf0..50ac29` reads wall-clock time, requests AlarmMon at priority 7,
non-positionally and full volume, and advances a shared deadline by 15000 ms.
This is independent of the faction's general attack-warning delay.
`tools/re/probe_monarch_alarm.py` executes the native cooldown/playback slice
with a controlled millisecond clock and observes the audio request. It checks
first playback, suppression, and the exact cooldown boundary; it does not
launch retail or replace any game assets.

The engine captures a cosmetic player bitmask when weapon damage reaches
a monarch, including direct, splash, and fatal hits. It carries ownership at
impact, rather than trying to infer it from a later render snapshot. The client
coalesces pending bits under the existing impact-queue mutex, independently of
that queue's eviction limit. Only the local player's warning is admitted;
spectators are excluded. Playback uses the existing global mixer and a
15-second real-time deadline. General unit alarms are a separate path.

This event is display-only: it changes no gameplay values, RNG, pathfinding,
network commands, or simulation hashes. The event lives in a shared simulation
header, so changing it means rebuilding every executable, including the server.

The checks cover the native request/cooldown behaviour, local/enemy/spectator
filtering, direct/fatal and splash damage, zero damage, tick-event clearing,
and decoding the installed AlarmMon WAV.
