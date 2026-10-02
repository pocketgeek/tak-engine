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

Full release-preparation suite results and tagged CI/package verification will
be recorded here as the release completes. No public-server deployment is part
of this release operation.

## Limits

Windows/macOS GPU rendering, real high-DPI/device-loss transitions and prolonged
dense combat were not visually validated locally. Native CI validates builds,
portable rendering tests, packaging and static dependencies, not interactive
GPU appearance. Remote multiplayer sweeps were not repeated for this release.
