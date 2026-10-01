# TAK Engine 0.7.19

- Add native Windows 11 ARM64 Release and Debug portable packages, with native
  compiler, executable-architecture, static-dependency and startup checks.
  Windows x64 retains its installer and portable packages.
- Add native Intel x64 macOS app bundles, DMGs and Release/Debug ZIPs alongside
  Apple Silicon packages. Both architectures target macOS 14 or later and verify
  bundle signatures, executable architecture and system-only dynamic imports.
- Correct Windows ARM64 x264/FFmpeg target selection. CPU streaming is available;
  the pinned FFmpeg excludes Windows ARM64 NVENC, and Qualcomm hardware encoding
  is not implemented.
- Handle legacy map-path conversion errors under libc++ and preserve successful
  static dependency builds in CI caches.
- Remove 22 unused engine helpers/fields and two obsolete tool blocks; update
  comments and research references. Live pathfinding and gameplay logic are
  unchanged, with deterministic simulation checks covering the cleanup.

Protocol remains **211**, campaign payload **4**, database schema **9**.
Use matching engine builds and compatible game data on all clients and servers. No new dynamic dependencies or bundled retail data are added.

See the [validation report](https://github.com/pocketgeek/tak-engine/blob/main/docs/release-0.7.19-validation.md).
