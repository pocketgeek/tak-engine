# TAK Engine 0.7.21

- Add named, selectable override packs under `overrides/<pack>/`. Multiple packs
  can be enabled, with separate remembered host and guest cosmetic selections.
  Files directly in `overrides/` are never loaded. Alphabetically later packs
  take precedence when they replace the same file.
- Transfer Full host packs to the server and other players, verify checksums
  before starting, and isolate each room's gameplay data. Guest packs remain
  cosmetic-only. Off hides the list. Campaigns retain retail data.
- Reload cached artwork, cursors, fonts and sounds when applying lobby packs;
  record shared pack identity in replays and recover corrupt cached downloads.
- Show game-create settings as read-only lobby information, including balance,
  monarch rules and random-map settings. Override mode changes on game creation;
  lobby pack checkboxes remain selectable. Remove misleading map-ready flashes.
- Avoid resending pending create/join/watch requests on slow links, preventing
  normal lobby entry from triggering the server flood limit.
- Give each running match its own simulation worker while keeping socket and
  shared-state ownership on the server network thread.
- Add verified native TLS, account and resource budgets, paced transfers,
  command validation, and optional Linux service isolation for public hosting.
  Server names default to TLS on port 7677.
- Add built-in Let's Encrypt issuance and renewal. The HTTP-01 listener opens
  on port 80 only when a challenge needs it.

**Compatibility:** protocol 213, replay format 10. Update clients and servers
together. Protocol-212 format-9 recordings retain playback compatibility;
Full-pack recordings require their verified `OverrideCache/` package.
Campaign payload 4 and database schema 9 are unchanged.

TLS/ACME dependencies are linked statically; no new dynamic runtime libraries
or retail assets are distributed. Screenshots have been freshly captured.
See [validation](release-0.7.21-validation.md) and
[public-server deployment](public-server.md).
