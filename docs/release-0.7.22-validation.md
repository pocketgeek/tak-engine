# 0.7.22 release validation

Protocol **213**, replay format **10**, campaign payload **4**, database schema **9**.

## Preparation evidence

- The engine revision entering release preparation passed all seven Linux
  distribution jobs, Windows x64/ARM64 and macOS x64/ARM64:
  [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/37039837038),
  [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/37039837042),
  [macOS](https://github.com/pocketgeek/tak-engine/actions/runs/37039837097).
- [Determinism and core tests](https://github.com/pocketgeek/tak-engine/actions/runs/37039066715)
  passed after the portable cancellation change. Later changes were confined to
  the ACME test fixture and CI test build commands.
- All 15 independent AA combinations passed the Linux OpenGL renderer test and
  ran in sparse and 1,200-unit scenes. Settings, migration, resource limits,
  transparency, clipping, native UI, target reuse and reset coverage passed.
  See [AA validation and measurements](antialiasing.md) for costs and limitations.
- Focused Release and Debug cancellation, map-transfer, parser-limit and AA
  tests passed. The ACME multi-source fixture passes using IPv4/IPv6 loopback,
  without contacting a public certificate authority.

## Release checks

- Full local GCC Release suite: **169/169** CTests passed.
- Full local GCC Debug suite: **177/177** CTests passed.
- All pre-tag platform checks passed at release commit
  `28ae364194ef55141529d2a2812855702b542cc0`:
  [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/37043817354),
  [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/37043817219),
  [macOS](https://github.com/pocketgeek/tak-engine/actions/runs/37043817074).
- Tagged checks passed for all seven Linux distributions and both native
  architectures of Windows and macOS:
  [Linux](https://github.com/pocketgeek/tak-engine/actions/runs/37045311518),
  [Windows](https://github.com/pocketgeek/tak-engine/actions/runs/37045311506),
  [macOS](https://github.com/pocketgeek/tak-engine/actions/runs/37045311526).
- Windows Azure OIDC signing succeeded. CI signed and verified all 16 shipped
  Release/Debug executables before ZIP/NSIS packaging, then signed and verified
  the installer, including authenticated timestamps.
- All **19 assets** were downloaded and their SHA-256 digests checked. ZIP CRCs,
  executable architectures, embedded PE signatures and macOS app versions passed
  inspection. Debian, RPM and Arch packages contain the systemd service and
  configuration templates; the Linux Debug tarball passed archive listing.
- Local Release and Debug client/server binaries were rebuilt with the clean
  **v0.7.22** stamp and report version 0.7.22.
- Platform uploads initially created separate release records alongside the
  staging draft. Their assets were consolidated and hashes rechecked before
  redundant drafts were removed. The Linux/macOS upload scripts now check for
  an existing release first; mocked existing-draft, first-create, concurrent-create
  and failure cases passed. This follow-up changes release automation/docs only,
  not the tagged engine or signed artifacts.

Release: [v0.7.22](https://github.com/pocketgeek/tak-engine/releases/tag/v0.7.22).
No public-server deployment was performed.

## Limits

Windows/macOS GPU rendering, real high-DPI/device-loss transitions and prolonged
dense combat were not visually validated locally. Native CI validates builds,
portable rendering tests, packaging and static dependencies, not interactive
GPU appearance. Remote multiplayer sweeps were not repeated for this release.
