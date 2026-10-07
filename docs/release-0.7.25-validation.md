# 0.7.25 release validation

Protocol **236**, replay format **11**, generator version **8**, campaign
payload **4**, database schema **9**.

Status: **in preparation.** Sections marked *pending* have not been run or
recorded yet. 0.7.25 supersedes the unpublished 0.7.24; its scope is everything
since the published [0.7.23](release-0.7.23-validation.md).

## Scope and compatibility

Two pathfinding modes ship: Retail (the default, used by campaigns and Crusades
battles) and experimental Legion. Retail+, Flowfield and Cooperative were
removed before release; the wire byte, replay header and saved preference accept
only Retail (0) and Legion (4), and a saved 1–3 falls back to Retail. Retail
behavior changed where the port was corrected against the retail binary
(flyer landing site and airborne grid, group pacing, path-budget class,
same-cell destinations, embarked-cargo exploration). Legion's known limits are
listed in [legion-pathfinding.md](legion-pathfinding.md#known-weaknesses).

Protocol 236 is required for live play. Replays need the exact simulation
protocol, so protocol-221 (0.7.23) recordings are not playable by this build.
Campaign payload 4 and database schema 9 are unchanged since 0.7.23; generator
version 8 replaces 0.7.23's generator 7, whose recipes cannot be regenerated.

*Pending:* confirmation that the tagged build reports protocol 236 from both
client and server, and any further scope notes from the lead.

## Local preparation checks

*Pending (agent release-local):* Release and Debug CTest sweeps, Python
research-tool suite, determinism runs across compilers and optimization levels,
`--mpai` Retail and Legion hash reproducibility, sanitizer checks, navigation
checkpoints and crowdbench screens.

## Remote multiplayer sweep

*Pending (agent release-remote):* the remote scenario table for Retail and
Legion, including negative desync/override gates, delay, jitter and packet loss.

## CI and packages

*Pending:* pre-tag CI, tagged CI on all four workflows, asset download and
SHA-256/size checks, Windows signing verification and package payload checks.

## Screenshots

All thirteen README and Cartographer screenshots were freshly captured on
2026-10-06 from the 0.7.25 preparation build (`takclient` reporting
**0.7.25**, build `v0.7.23-197-g3490f5544e65`, optimized Debug, the
`release-docs` branch at `origin/main` 3490f55) and visually inspected. Game and
menu views used the offscreen video driver with OpenGL rendering (confirmed in
each log); Cartographer views used the dummy driver and software renderer. Each
capture used isolated `XDG_DATA_HOME`/`XDG_CONFIG_HOME` profiles, never a user
profile. Both scratch servers reported protocol v236. The diplomacy fixture and
the Cartographer model-inspector workflow test printed PASS.

Nine images changed. `campaign.jpg`, `crusades.jpg`, `results.jpg` and
`cartographer.png` were re-rendered but are byte-identical to the previous
refresh. The room fixture now selects Legion, because Retail+ no longer exists.
The JPEG encoder reproduces the previous published bytes exactly from the
previous raw capture, so differences come from rendering only. The capture
notes in [the image directory](img/README.md) identify development demos and
sample results rather than live-game measurements.

## Limits

Local visual inspection covers Linux OpenGL screenshots and the software
Cartographer renderer. Interactive Windows/macOS GPU rendering, physical
high-DPI displays and a multi-hour combat soak are *pending* or out of scope;
the lead will record which.
