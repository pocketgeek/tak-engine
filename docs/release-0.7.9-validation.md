# 0.7.9 release validation

Release checks performed on 2026-09-27. Network protocol remains 189.

## Scope

Windows filename/startup/audio fixes; native GPU telemetry; retail-style F4
scoreboard and side-panel score; read-only lobby sight/radar status; and a
placement-grid hash optimization that preserves the existing hash stream.

No retail GUI was launched. No game assets or vendor runtime libraries were added
to the repository or packages.

## Checks

All targets rebuilt in all four configurations:

| Configuration | CTest result |
| --- | --- |
| Release | 85/85 passed |
| Optimized Debug (`-O2 -g`) | 88/88 passed |
| Debug | 88/88 passed |
| Clang Debug | 88/88 passed |

The unchanged pre-version-bump source passed Windows, macOS ARM64, and all seven
Linux package jobs on main (`afd6bfc`). Tag workflows build and attach the 0.7.9
packages and repeat their platform-specific startup, media, and determinism gates.

The deterministic-math guard and available GCC/Clang optimization comparisons
passed with golden `dcef618cd2e4d558`. Local ARM emulation legs skipped because
target libc/headers are unavailable; macOS CI covers native ARM64.

Earlier targeted checks on the included code:

- Actual map catalog: 1,343 maps, including legacy CP1252 archive names and
  accented UTF-8 disk filenames. The previous Windows conversion exception was
  reproduced under Proton; the corrected executable passed.
- Audio-device regression: absent endpoints do not trigger repeated opens;
  available and unknown-enumeration endpoints retain normal open behavior.
- GPU tests cover unsupported counters, real zero utilization, 0–100% bounds,
  multi-process engine aggregation, parallel engines, and multi-adapter identity
  and memory consistency. Direct NVML reports valid readings on the local NVIDIA
  GPU. Cross-compiled Windows tests passed under Proton, where native GPU counters
  are unavailable. Physical AMD/Intel Windows readings remain unverified.
- F4 screenshot smoke check verified compact rows, retail faction emblems,
  white in-game serif text, and Name/Kills/Losses/Score columns. Existing font
  users retain their default spacing; scoreboard glyphs use tighter spacing.
- Ultima Online B1 before/after runs preserved all 60 eight-unit checkpoints and
  all 120 mixed 256-unit checkpoints. Average hash time in the small run fell
  from 25.41 ms to 7.29 ms. See [measurements](large-map-hash-performance.md).

The full remote multiplayer and native-animation sweeps from 0.7.8 were not
repeated for this release. This patch does not change simulation behavior or
network message formats. The reported sustained large-map speed fluctuation
remains unconfirmed as fixed.
