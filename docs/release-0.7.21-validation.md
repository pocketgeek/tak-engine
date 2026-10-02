# 0.7.21 release validation

Protocol **213**, replay format **10**, campaign payload **4**, database schema **9**.

## Local checks

- GCC Release: **165/165** CTests.
- Optimized GCC Debug: **173/173** CTests.
- GCC Debug: **173/173** CTests.
- Python research/tooling unit tests: **151/151**.
- Clang AddressSanitizer/LeakSanitizer: **5/5** focused tests covering room workers,
  override packages, replay serialization, connections, and command validation.
- Deterministic math: all **9** GCC/Clang x86-64 and GCC ARM64 builds at O0/O2/O3
  agree on `dcef618cd2e4d558` (ARM64 runs under QEMU).
- Override integration covers host upload, separate server/peer caches, multiple
  selected packs, Off/Full transitions, pack replacement, cosmetic isolation,
  remembered selections, corrupt-cache recovery and 90 lockstep ticks.

## Remote and release checks

Remote multiplayer, local Pebble ACME integration and release-package verification
are in progress. Results will be recorded here before declaring release complete.
Remote tests use isolated optimized test servers on `tak.pgnet.us` and
`vpn3.pgnet.us`; public server processes and installed override files are not changed.

## Screenshots and documentation

All **13** screenshots were freshly rendered and visually inspected, including
an added lobby view showing the pack list and read-only game settings.
[Capture notes](img/README.md) distinguish live UI, development scenes and sample
statistics. Ships in the naval capture are on water. No retail archives or
extracted assets are included in the release.

README, user guide and public-server documentation describe the released features
and compatibility requirements. Windows/macOS interactive gameplay is not tested
locally; native CI and package checks provide platform validation.
