# 0.8.10 release validation

Protocol **289** (was 288), replay format **14**, generator version **9**,
campaign payload **4**, database schema **9**.
<!-- LEAD: re-check kNetVersion and kReplayFormat right before tagging (289 / 14
     at c73416f0). If either moves again, update the protocol lines in these
     notes, the release notes, README.md ("New in 0.8.10", protocol lines),
     docs/user-guide.md (intro, Versions, Replays, troubleshooting),
     docs/public-server.md, docs/random-map-generation.md and
     docs/multiplayer-design.md. -->

TODO-LEAD: Tagged `v0.8.10` at `<sha>` (annotated tag object `<obj>`) and published on `<date>` UTC.

## Scope and compatibility

0.8.10 follows 0.8.9 (protocol 288). After the 0.8.9 validation commit
`b35d63b9`, main reached `c73416f0` in 17 commits, all linear (no merges).
`git diff --stat b35d63b9..c73416f0 -- src` touches 27 files:
`src/server/server.cpp`, `src/server/acme.cpp`, `src/server/acme_internal.h`;
`src/net/` (`client.cpp`, `client.h`, `conn.cpp`, `protocol.cpp`, `protocol.h`,
`revocation.cpp` and `revocation.h` (both new)); `src/sim/` (`matchsetup.cpp`,
`matchsetup.h`, `mission.cpp`, `retailcost.h`, `sim.cpp`); `src/client/`
(`gameview.h`, `gameview_impl.cpp`, `gameview_net.cpp`, `gameview_render.cpp`,
`main.cpp`, `settings.cpp`, `settings.h`, `streaming.cpp`, `streaming.h`);
`src/video/stream.cpp`, `src/video/stream.h`; and
`src/cartographer/playtest.cpp`. Nothing under `src/ai/`, `src/campaign/`,
`src/tnt/`, `src/terrain/`, `src/cob/` or `src/crt/` changed.
TODO-LEAD: commits that landed on main after `c73416f0` before the preparation
commits (none expected), and the list of preparation commits.

The changes:

- Disconnect wait and AI takeover (server, net, client; protocol 289):
  `deb6a31b` (wait with WAITING FOR countdowns, `Event::AiTakeover` to a Normal
  server AI, short reconnect from the first missing tick with `Reseated`, no
  world rebuild; removed the 5 min slot hold, the 2 min pause budget and its
  `Forfeit`, the full-log seated rejoin, `TAK_RESUME` and `--mprejoin`; quit
  from the menu sends `LeaveGame`), `96a4a6cf` (a countdown begun before the
  first tick runs out; reconnect with a login), `82bb831d` (one 30 s budget per
  seat per game), `bf14a9e4` (`Msg::Waiting` carries the recipient's own budget;
  the client reconnects within it), `b2e14782` (budget spent from the last
  contact; holds past a 1 s grace charged), `c73416f0` (no charge for pre-game
  or loading silence; the client's reconnect window kept current).
- Streaming: `abdeea70` (SAVE A LOCAL COPY, fragmented MP4 in `videos/`,
  `streamLocalCopy` setting; FFmpeg `--enable-muxer=flv,mp4`).
- Campaign: `ec38fdfa` (the human is always Player1 / world slot 0; fixes
  `takx22_mt`, `takx24_mt`), `13477a0b` (`replay_test` pins 289 and refuses 288).
- Cartographer: `23ecea9c` (Test Map finds the game from inside the macOS app
  bundle, DMG and ZIP layouts), `7df207d4` (guide), `57ce78e1` (Windows test
  expectation).
- Simulation: `e19580b9` (retail terrain-scaled turn rate in the target score's
  turn penalty, `retailEffectiveTurnRate`; `fire_sweep_turnrate`).
- Public server TLS: `7b674dd5` (ACME Renewal Information, RFC 9773, with
  `replaces`; CRL revocation checks in the TLS client and the renewal worker),
  `5552c586` (`replaces` only for a described certificate; window clipped at
  expiry).
- Docs: `cd2df9d7`, `b2999ea9`.

The disconnect accounting is server wall-clock only and changes no simulation
state until a takeover; the campaign seat changes hashed state for `takx22_mt`
and `takx24_mt` only; the turn-rate change reaches only shooters on roads or
water. `--mpai` Inner Circle seed 1 at 60 s is unchanged per `deb6a31b` and
`e19580b9`: Retail `b353410003e98dd5`, Legion `0825aeb9e8139203`.
TODO-LEAD: 300 s on the final main (0.8.9: Retail `a3d953803a064191`, Legion
`ce5c4ba7562e53b9`); whether they moved and why.
TODO-LEAD: `--mpai` on Windows and macOS (optimized Debug builds of the tag).

**Compatibility.** `kNetVersion` is **289** in `src/net/protocol.h` at
`c73416f0` (288 at `v0.8.9`). `kReplayFormat` is **14** in
`src/net/replayhdr.h`, as at `v0.8.9`. The map generator's `formatVer` **9**
(`src/tnt/mapgen.h`), the campaign payload's `kVersion` 4
(`src/net/crusades.h`) and `kSchemaVersion` 9
(`src/server/crusades/store.cpp`) are unchanged.

- **Multiplayer.** The server's Hello check rejects a client whose protocol
  differs from `kNetVersion` (or whose gameplay-data hash differs). So **0.8.10
  cannot play with 0.8.9, 0.8.8 or earlier in either direction**; the server and
  every player must update together.
- **Replays.** `supportedReplayProtocol` accepts a recording only when its
  protocol equals `kNetVersion`, so 0.8.10 refuses 0.8.9's and 0.8.8's
  protocol-288 replays (format 14 as well) and 0.8.9 refuses 0.8.10's.
  `tools/replay_test.cpp` pins `kNetVersion == 289` and `kReplayFormat == 14`
  and checks that `supportedReplayProtocol(14, 288)` is refused.
- **Maps, mods and settings.** Saved `.kmp` maps, generator-8/9 recipes,
  `mods/` packs and `ModCache/*.takmods` carry over. One settings key was added
  (`streamLocalCopy`, default off); none was removed.

## Local preparation checks

TODO-LEAD: worktree, commit, toolchains, cores, load.

**Result:** TODO-LEAD.

- **Multiplayer and replays.** TODO-LEAD.
- **Builds.** Release, Debug, optimized Debug (`-O2`), Clang Debug and
  AddressSanitizer all-target builds: TODO-LEAD (steps; warning count vs 0.8.9's
  950 lines).
- **Full CTest.** TODO-LEAD (registered tests vs 0.8.9's 742 Debug / 698
  Release; added and removed tests).

  | Tree | Selection | Result | Time | Failures |
  | --- | --- | --- | ---: | --- |
  | Release | `-LE nightly` | TODO-LEAD | | |
  | Release | `-L nightly` | TODO-LEAD | | |
  | Optimized Debug | `-LE nightly` | TODO-LEAD | | |
  | Optimized Debug | `-L nightly` | TODO-LEAD | | |
  | Debug (`-O0`) | `-LE nightly` | TODO-LEAD | | |
  | Clang Debug | `-LE nightly` | TODO-LEAD | | |
  | ASan | `-LE nightly` | TODO-LEAD | | |

  - **Disconnect tests.** TODO-LEAD (`server_disconnect`, `disconnect_client`
    in every tree).
  - **Streaming local copy.** TODO-LEAD (`stream_test` with the MP4 muxer, not
    skipped with 77).
  - **The settings-leak guard.** TODO-LEAD (`pref_guard_setup`/`pref_guard_check`;
    real profile's `settings.ini` SHA-256 and mtime before and after).
- **`--mpai` golden hashes.** Optimized Debug build, Inner Circle, `takserver
  --local --no-auth --seed 1`, an empty `XDG_DATA_HOME`, `TAK_LEGION=1` for
  Legion.

  | Mode | Duration | `<final main>` (289) | 0.8.9 (288) |
  | --- | ---: | --- | --- |
  | Legion | 300 s | TODO-LEAD | `ce5c4ba7562e53b9` |
  | Retail | 300 s | TODO-LEAD | `a3d953803a064191` |
  | Legion | 60 s | TODO-LEAD | `0825aeb9e8139203` |
  | Retail | 60 s | TODO-LEAD | `b353410003e98dd5` |

- **Determinism.** TODO-LEAD (`tools/check-determinism.sh` golden; 0.8.9:
  `dcef618cd2e4d558`; `tools/check-detmath.sh`).
- **Legion scenario gate.** TODO-LEAD (145 files in 0.8.9; key counts vs 0.8.9's
  `36522 keys checked, 0 failed, 4415 ratchets, 4705 licensed, 266 floor
  exceptions, 864 report-only`).
- **Retail group emulation check (formation-air).** TODO-LEAD.
- **Python research suite.** TODO-LEAD (165 tests in 0.8.9; archive verifier 6
  tests; retail SHA-256 before and after).
- **Syntax checks.** TODO-LEAD (tracked Python files compile; `bash -n`).

Logs: TODO-LEAD (`~/tak-tmp/scratch/release-0810/`).

**Final source.** TODO-LEAD (after the preparation commits, the optimized Debug
tree rebuilt every target (version `0.8.10`) and passed `ctest -L quick`).

## Remote multiplayer sweep

TODO-LEAD: the 37-scenario table for Retail and Legion (date, commit, build id,
hosts, retries, gates: build identity, planted desync, game mod transfer).

| Mode | Completed scenarios | Completed seats | Client-pair checkpoints | Client-referee checkpoints |
| --- | ---: | ---: | ---: | ---: |
| Retail | TODO-LEAD | | | |
| Legion | TODO-LEAD | | | |

### Reference ticks and hashes (0.8.10 candidate)

TODO-LEAD: per-case table vs 0.8.9 (protocol 289 vs 288: same harness, hosts
and seeds; the six `TAK_AUTOPLAY` cases and the load-sensitive `h-lake`,
`h-stress`, `2h-stress`, `2h-gods`, `4h-gods`, `h-everything` are noise).

### Comparison with 0.8.9

TODO-LEAD. Expected: the disconnect changes are server wall-clock accounting
and touch no hashed state while every seat stays connected; the campaign seat
fix reaches only `takx22_mt`/`takx24_mt` (not in the sweep); the target-score
turn rate can move cases where shooters stand on roads or water. Explain every
deterministic case that differs from 0.8.9.

## CI and packages

TODO-LEAD: main CI on the final source (Determinism, Windows, Linux packages,
macOS); tag CI runs and reruns (known flakes: macOS `stream_test` timing, "Upload
CI artifact: ENOTFOUND", macOS x64 `hdiutil` "Resource busy", Fedora openh264
mirror, Slackware mirror `md5sum`, `gitlab.freedesktop.org`, `downloads.xiph.org`
opus, Ubuntu 22.04 `crusades_client_network_test`, GitHub runner hang in
determinism.yml 'Install build deps').

- TODO-LEAD: 29 assets downloaded, SHA-256 digests and sizes vs GitHub, size
  ratios vs 0.8.9 (FFmpeg now carries the MP4 muxer, so every `takclient` is
  expected to grow slightly).
- **Build ids.** TODO-LEAD (`0.8.10 (build v0.8.10)` on Linux, Windows, macOS;
  installer and DMG names carry `0.8.10`).
- **Windows signatures.** TODO-LEAD (9 Authenticode Valid in 0.8.9).
- **macOS notarization.** TODO-LEAD (both DMGs and both apps, DMG and ZIP).
- **macOS Test Map.** TODO-LEAD (Cartographer from the DMG and from the ZIP
  starts the game's private lobby with Test Map).
- **Streaming local copy in packages.** TODO-LEAD (the packaged FFmpeg has the
  MP4 muxer: a short local copy on at least one platform, or `stream_test` in CI
  not skipped).
- **Opus is static; font licences; mods folder; macOS microphone and Retina;
  no libsecret/GLib `NEEDED`.** TODO-LEAD (as in 0.8.9).
- TODO-LEAD: `docs/release-0.8.10-notes.md` and this report in the source tree
  at the tag; `kNetVersion` there is 289.

TODO-LEAD: Published `<date>` UTC with `gh release edit v0.8.10 --draft=false --latest`.

## Screenshots

TODO-LEAD: retaken on the shots branch (title.jpg VERSION 0.8.10, the YouTube
Streaming panel with SAVE A LOCAL COPY, the disconnect banner if retaken);
README title alt text and gallery footnote, `docs/img/README.md` header, rows and
scratch list.

## Limits

- No game mixing 0.8.10 with 0.8.9 or older was attempted: the protocols differ,
  so the server refuses it.
- All of 0.8.9's limits carry over: Legion units stand and wait more than
  Retail's; Legion attackers through a narrow gate deal more damage than
  Retail's; power codes are lockstep (retail applied them only on the typer's
  machine); FULL VISION does not switch LOS off at start; UI FONT CLASSIC is not
  pixel-identical with 0.8.7; the Legion bridge lottery; the Legion 10,000
  workloads' congestion and tick budget, mixed-10000's hovering flyers; Retail
  `h-cramped` ends early; the macOS Retina UI Scale is migrated once; a
  stationary auto-targeting shooter keeps refusing a once-blocked target; the
  campaign book's chapter numeral touches "APTER".
- The macOS Cartographer Test Map limit is fixed (`23ecea9c`).
- New with the disconnect wait: a server-side upload stall longer than 1 s is
  charged to every seat it holds up; a reconnecting seat that hitches while
  catching up is charged as a hold.
- TODO-LEAD: flakes seen in this release's runs.
