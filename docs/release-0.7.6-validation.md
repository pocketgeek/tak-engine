# 0.7.6 release validation

Local checks performed on 2026-09-26 using the engine source at `56047fe`
plus the release preparation changes (version, documentation, screenshots,
CI coverage, and stronger stereo assertions). No simulation code changed during
release preparation.

## Builds and automated tests

| Configuration | Result |
| --- | --- |
| Release, all targets | 76/76 CTest tests passed |
| Debug with `-O2 -g`, all targets | 79/79 CTest tests passed |
| Debug, all targets | 79/79 CTest tests passed |

The optimized suite was also rerun after the final versioned rebuild: 79/79.
The menu-music tests cover format/rate conversion, distinct left/right channels,
volume and mute, looping, callback detachment, and loading/reopening the actual
retail title track. CI now runs the synthetic menu-music test on Linux, Windows,
and macOS without needing retail assets.

`tools/check-determinism.sh` passed the compiler/optimization comparisons with
hash `dcef618cd2e4d558`. The local ARM emulation leg was unavailable; the macOS
workflow separately tests deterministic math and driver equivalence on ARM64.

Native retail routine probes passed for the monarch alarm and all 216 scoring
cases, including campaign score GET/SET. These execute isolated routines and do
not launch the retail game. See [monarch alarm evidence](monarch-attack-alarm.md)
and [campaign/scoring evidence](campaign-presentation-2026-09-26.md).

## Remote multiplayer sweep

The complete 37-scenario table in `tools/desync-hunt-remote.sh` passed across
`tak.pgnet.us` and `vpn3.pgnet.us`: **52 client seats completed, no desyncs, no
incomplete runs**. Each case requested five simulated minutes, with legitimate
match conclusions accepted earlier. Coverage included Retail/Crusades balance,
naval maps, fog/start rules, unit caps, stress, live orders, multiple human seats,
latency/jitter, packet loss, and two all-AI spectator cases.

The two spectator cases test flow/completion only, not hash consensus. The
spectator stress case reached tick 9,000 with 8,137 surviving units. Live-order
cases establish consensus within each match, not reproducible hashes across
separate runs.

The harness also correctly detected a deliberately planted desync at tick 900
and rejected a deliberate gameplay-data mismatch. The temporarily moved remote
override was restored. Servers used isolated GCC 13 optimized Debug (`-O2 -g`)
binaries; production server binaries were not replaced. They contained the
release engine changes but retained the pre-release version stamp used when
the sweep started. Subsequent changes were version metadata, tests, and docs.

## 16,000-unit simulation smoke tests

All four `simperf` modes completed 900 ticks (30 simulated seconds) on the
local machine. Match mode used Crusades balance, mixed retail armies, and eight
Absurd AIs; the other modes used the default balance. These were generated-map,
headless runs, not rendered game benchmarks or sustained performance targets.

| Workload | Ending units | Observed simulation speed |
| --- | ---: | ---: |
| Synthetic movement | 16,000 | 2.853× |
| Mixed-unit patrol | 16,000 | 1.074× |
| Destination congestion | 16,000 | 1.159× |
| AI combat | 14,799 | 1.057× |

Host: Core Ultra 9 275HX. Other release checks were active, so these timings are
smoke-test observations rather than controlled performance comparisons.

## Streaming and presentation

- Local encrypted-upload, reconnect, wrong-hostname rejection, and key-redaction
  tests passed with `tools/check-stream-network.py`.
- An offscreen OpenGL capture test took a 7680 × 2160 source through the 4K/60
  streaming preset: 226 frames encoded, zero dropped, average capture 2.30 ms.
  The requested swap interval remained 1. This was a local encoding test, not a
  live YouTube broadcast or proof of monitor pacing under the offscreen driver.
- All seven README scenes were recaptured and visually inspected. Unchanged
  scenes can produce identical image files. See [capture details](img/README.md).
- README local links and `git diff --check` passed.

These checks cover the tested paths and workloads; they do not claim exhaustive
visual equivalence with retail or performance on every GPU.
