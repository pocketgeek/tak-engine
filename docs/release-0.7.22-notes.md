# TAK Engine 0.7.22

- Replace whole-scene antialiasing with independent Terrain AA (Off/2x/4x)
  and Model AA (Off/2x/4x/8x/16x). Preserve model/scenery ordering and keep UI
  native. Migrate old preferences, bound target memory and report effective
  fallback levels. Higher model AA can cost more frame time and memory; see
  [the measurements and rendering design](antialiasing.md).
- Bound TDF section/node counts and parsed memory while preserving duplicate
  sections and case handling. Validate untrusted map packages on a bounded
  background worker, with cancellation independent of connection lifetime.
- Check actual package sizes and remaining map-memory admission budgets across
  cache, installed-map, upload and override paths.
- Bound player-command byte/command processing, parse once, stop work at limits
  and aggregate overflow logging while retaining ownership checks.
- Recover automatically from transient ACME startup failures and persisted CA
  backoff. Limit HTTP-01 connections per source so one client cannot consume all
  challenge slots. Keep the macOS 14 SDK supported.
- Package the Linux systemd service, ACME template and configurable server
  settings, with [step-by-step instructions](public-server.md). Set your own
  hostname and review configuration before enabling the service.
- Continue clearing build sites containing multiple reclaimable obstructions.
- Sign and timestamp Windows Release/Debug executables before packaging, then
  sign the completed installer. Verify signatures before publishing. ARM64
  outputs are signed on a Windows x64 runner through Azure/GitHub OIDC.
- Run AA and cancellation regressions in native Linux, Windows and macOS CI.

**Compatibility:** protocol 213, replay format 10, campaign payload 4 and
database schema 9 remain unchanged. Use matching builds and gameplay data.
Oversized custom map definitions can now be rejected by explicit parsing limits;
an installed-map cache miss may require a first-use upload from the host.

No new dynamic runtime dependencies or retail archives are included.
See the [validation report](release-0.7.22-validation.md). Existing README
screenshots remain labeled as 0.7.21 captures; AA comparisons are documented
separately.
