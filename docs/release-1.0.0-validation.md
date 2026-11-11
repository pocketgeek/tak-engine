# 1.0.0 release validation

Protocol **323**, replay format **15**, random map generator version **10**.

Tag `v1.0.0` -> `TODO-LEAD` (the version commit); annotated tag object `TODO-LEAD`.
Published TODO-LEAD UTC as the Latest release.

## Scope and compatibility

1.0.0 is main at `bd0cccd6` plus the version commit `TODO-LEAD`. Its changes since the
last development version, 0.9.24 (tag `v0.9.24` -> `ec50a785`), are listed in the
CHANGELOG's 1.0.0 entry; `git log v0.9.24..bd0cccd6` holds them.

**Compatibility.** 1.0.0 plays with no earlier version and plays none of their replays
(`replay_test` refuses another protocol). TODO-LEAD: what the `--mpai` gates show on
shipped maps.

## Local tests on the cut

Linux, `build-o2` (Debug `-O2 -g`), on `TODO-LEAD`:

- **Full ctest** (`ctest -j 16`, every label): TODO-LEAD.
- **`--mpai` Inner Circle, seed 1**, `TAK_SPEED=80`: TODO-LEAD.

| Gate | 0.9.24 | 1.0.0 `TODO-LEAD` |
|---|---|---|
| Retail 60 s | `beb81788610d2a6c` | TODO-LEAD |
| Legion 60 s | `87efa5106c2d3c0e` | TODO-LEAD |
| Retail AI 60 s (`TAK_AI_LEVEL=6`) | `a3ddc1ba59738936` | TODO-LEAD |
| Retail 300 s | `30a3e25d3f414fad` | TODO-LEAD |
| Legion 300 s | `bd274b29f05f2c18` | TODO-LEAD |
| Crusades, Retail 300 s | `23d5e2016eae1bb8` | TODO-LEAD |
| Crusades, Legion 300 s | `ee9c38ff55448ab3` | TODO-LEAD |

- **Determinism** (`tools/check-determinism.sh`, `tools/check-detmath.sh`): TODO-LEAD.
- **Doc links** (`legion_doc_links`, `legion_doc_links_selftest`): TODO-LEAD.
- **Pictures** retaken for 1.0.0 (`docs/img/README.md`): TODO-LEAD.

## Remote sweep

TODO-LEAD: the remote desync sweeps (hosts, maps, modes, seeds, games played, hash
agreement).

## Windows and macOS

TODO-LEAD: Windows local suite on oden-win (`runctest.py` / `wintest.sh`) and macOS
local suite on the MacBook: tests run, passed, failures and their causes.

## CI and packages

TODO-LEAD: tag CI on `v1.0.0` (Determinism (sim), Windows build, Linux packages, macOS
build, both Sign + notarize jobs) with run ids and attempts; the asset count, all assets
downloaded and matched against the release's SHA-256 digests; the build string `v1.0.0`
in a Linux package's `takclient` and `takserver`; the new AppImage, openSUSE Tumbleweed
and EL9 / EL10 packages installed and started.

## Signing

Windows: TODO-LEAD (`Get-AuthenticodeSignature` on the x64 setup program and
`takclient`, `takserver` and `cartographer` in both zips; version string).

macOS: TODO-LEAD (`stapler validate`, `spctl -a -vv` and `codesign --verify --strict
--deep` on both DMGs and every app copy; version string).

## Publication

TODO-LEAD: the draft created with the notes (validation link rewritten to its `blob`
`v1.0.0` URL) before tag CI uploaded into it; publication time; asset count; the notes'
links answering 200. Every earlier GitHub release removed after publication, their tags
kept: TODO-LEAD (`gh release list` afterwards, `git ls-remote --tags` still listing every
`v*` tag).

## Known limits

- TODO-LEAD
