# TAK Engine 0.9.4

0.9.4 follows 0.9.3. It removes the unfinished Darien Crusades online-campaign
meta game from the client, the server and the packages, and it fixes the
Crusades *balance* (the room's CRUSADES option) to match the original game: the
same units and the same build menus.

**Compatibility first.** Both changes touch the network messages or the game
simulation, so 0.9.4 uses network protocol **298** (replay format **15**,
unchanged). **0.9.4 does not play with 0.9.3 or any earlier version, and does
not play their replays.**

- **Everyone updates together.** A 0.9.4 player cannot join a 0.9.3 server, and
  a 0.9.3 player cannot join a 0.9.4 server: the connection is refused with a
  *protocol version mismatch* message. Update your game and tell the people you
  play with to update theirs.
- **Old replays:** a replay recorded by 0.9.3 or earlier is refused with the
  version message. To keep watching them, keep your 0.9.3 install (0.9.0 to
  0.9.3 stay on the GitHub releases page); versions before 0.9.0 are no longer
  offered for download and can be built from their git tags (see the
  [development guide](development.md)).
- **Players:** nothing else to do. Settings, your data folder choice, key
  bindings, saved `.kmp` maps, generated-map recipes, mod packs and their ticks,
  and `ModCache/` downloads carry over, and so does the remembered CRUSADES
  choice of the game setup screen. Settings keys of the removed meta game, if
  any, are dropped the next time the settings are saved. The `ReplayCache/`
  folder beside `settings.ini` (downloaded Crusades battle replays) is no longer
  used and can be deleted.
- **Server operators: what to remove.** Update `takserver` to 0.9.4 when you
  announce the release; until you do, 0.9.4 players cannot use your server.
  Then, at your convenience:
  - take `--crusades-db` and `--crusades-definition` (and their values) out of
    your service drop-in or script. They no longer exist; a server still given
    them prints a warning that the option was removed and is ignored, and
    starts as usual;
  - delete the campaign database (`campaign.sqlite` and its `-journal`, `-wal`,
    `-shm` and `.service-lock` companions) and the `crusades-replays/` folder if
    you have them: nothing reads them any more;
  - stop using `crusades_admin`: it is no longer in any package.

  Accounts, the account file and the replay directory are untouched. See
  [public-server.md](public-server.md#versions).

## The Darien Crusades meta game is removed

Everything built for the online campaign is gone: the strategic map of Darien
and its campaign screens, the online campaigns, allegiance to a side, territory
matchmaking, the issued Crusades battles with their forfeit-on-disconnect rule,
the server's campaign database and its archive of battle replays, and the
`crusades_admin` tool. It was never offered in the game browser, so ordinary
skirmish, multiplayer and campaign play is unchanged: rooms, accounts,
spectating, reconnecting, replays, voice chat and game mods work as before.

- **What stays:** the **Crusades** option of a game room (the alternative unit
  balance from the final retail patch; `--crusades` in debug launches) and
  server accounts and sign-in, which never depended on the meta game.
- **No more Crusades labels.** The client no longer asks a server for Crusades
  campaigns after signing in; the lobby, the in-game menu and replay playback
  lose their Crusades-battle labels and rules (no more "ESC: RETURN TO
  CAMPAIGN").
- **Smaller packages.** `crusades_admin` is no longer in any package (Linux,
  Windows installer and zips, macOS), `takserver` no longer links SQLite, and
  the client no longer mounts the Boneyards campaign art it used only for the
  strategic map.
- **Simpler server rules.** With issued battles gone, every room writes its
  replay to the replay directory (when one is set) and allows spectators by its
  ALLOW SPECTATORS option, and a player whose connection runs out of time is
  always replaced by a server AI.

## Crusades balance as in the original game

The original game never reads the `canbuildcb/` build menus shipped with the
final patch, and it does not fall back to the standard unit files for a unit the
Crusades set lacks. With Crusades on, the units are now only those of the
Crusades unit set (`unitscb/`), and every build menu is the standard one,
limited to those units and in ascending `[Menu] Priority` order. Unit costs and
statistics, which is what the Crusades balance is really about, are unchanged.

- **Six Iron Plague units do not exist in a Crusades game:** the Trapdoor
  Spider, the Heligrin, the Cult of Lokken Ship, the Sea Serpent, the Mummy and
  the Iron Plague's second Lokken unit (`tarnecr2`; the Taros monarch Lokken is
  unaffected). A Crusades Beast Tamer could train the Trapdoor Spider before;
  the original game never offers it.
- **Gods are built by the monarchs again:** Elsin, Kirenna, Lokken and Thirsha
  build their side's god instead of the Acolyte, the Priest of Lihr, the Dark
  Priest and the Shaman.
- **The Zhon Shaman builds the Giant Orm and the Giant Barracuda**, and the
  **Beast Handler builds the Swamp Beast**.
- **The Beast Tamer has its nine standard entries** (Lodestone, Kraken,
  Gryphon, Spirit Wolf, Basilisk, Harpy, Death Totem, Beast Handler, Beast
  Lord), not thirteen.
- **The Ark has no build menu** (it built the Spyhawk, Swordsman and Archer in
  Crusades games).
- **The other menus that had moved are standard again:** the Grenadier comes
  from the Acolyte, not the Keep; the Flying Builder no longer builds the
  Rolling Tower; the Flagship no longer builds the Lighthouse; and the Ballista
  comes from the Priest of Lihr, not the Citadel, whose menu is back in its
  standard order.

Standard (non-Crusades) games are bit-identical to 0.9.3. Because `canbuildcb/`
is no longer game data, the game-data check value that players and servers
compare changes for every install; it matters only together with the protocol
change.

## For modders and mapmakers

- A menu entry without a `[Menu] Priority` now sorts last, as in the original
  game, and a unit without `builder=1` gets no build menu. The shipped data has
  neither, so this matters only for mods.
- With Crusades on, a mod's `canbuildcb/` files are ignored like the shipped
  ones; the `unitscb/` set is the roster. An install with no `unitscb/` plays
  the standard roster.
- No map, recipe or package format changed.

## For contributors

- Removed: `src/client/crusadesmap.*`, `crusadesscreen.*`, the campaign API in
  `MpClient`, `src/net/crusades.*`, `src/server/crusades/*`, `crusades_admin`,
  every `crusades_*` test and tool of the meta game, `cmake/SQLite.cmake` and
  `tools/re/crusades_territories*.py`. The 22 `Crusades*` protocol messages are
  gone, so message ids from `ModOffer` on moved down; bundle event kind 3 is
  reserved (refused by `getBundle`, ignored by the sim). Kept: the balance
  tools (`crusades_inventory`, `probe_crusades_damage`,
  `crusades_balance_audit_test`) and the research that documents the balance
  data in `docs/research/darien-crusades/`.
- Moved before the removal: the replay size cap (`kMaxReplayBytes`) to
  `net/replayhdr.h`, the service lease to `tak::srv::FileLease` (also the ACME
  state-directory lock), and the Python wire helpers of the server tests to
  `tools/server_net.py`. New test `account_auth_network` (register, login, wrong
  proof, restart, and a server given the removed flags with a leftover
  `campaign.sqlite`).
- Balance: `setupRegistry` loads the Crusades roster from `unitscb/` alone and
  menus from `canbuild/` alone (`loadBuildTree` keeps loaded builders and
  entries naming a loaded type, stable sort by priority); new `balance_roster_test`.
  The rule is documented in
  [crusades-balance-reference.md](crusades-balance-reference.md) and
  [retail-engine.md](retail-engine.md).
- `--mpai` Inner Circle seed 1: all five gate hashes are unchanged from 0.9.3
  (Retail 60 s `d375d03a6c472773`, 300 s `387df8b0916a182e`; Legion 60 s
  `426ab0842b709c28`, 300 s `2d09d13d3ef0d324`; Retail AI 60 s
  `c5846d769e0a7d93`); the cross-compiler golden (`check-determinism`) stays
  `dcef618cd2e4d558`. Crusades 300 s: Retail `3a380ae964f75b5a`, Legion
  `772874591d07dbdf`.

## Known limits

New with 0.9.4:

- **A Crusades-balance replay that contains one of the six removed units** cannot
  be played; every replay from 0.9.3 is refused anyway.
- **No mixed-version game was run** (0.9.3 with 0.9.4); the refusals rest on the
  protocol check and on tests (see the validation report).
- **macOS and Windows builds of the removal** are checked by tag CI only.

Everything listed under Known limits in the
[0.9.3 notes](release-0.9.3-notes.md#known-limits) still applies.

## Compatibility

Protocol **298** (was 297), replay format **15**, generator version **9** and
campaign payload **4** (the Crusades campaign payload and its SQL schema no
longer exist in the source). Checked in source against `v0.9.3`: `kNetVersion` in
`src/net/protocol.h` and `kReplayFormat` in `src/net/replayhdr.h`.

- **Multiplayer.** A server accepts a client whose protocol equals its own (and
  whose original game files match the server's); the release number is not
  sent. So **0.9.4 clients and servers play only with each other**.
- **Replays.** Playback requires the recording's protocol to equal the engine's,
  so 0.9.4 plays only 0.9.4 replays; a 0.9.3 replay (format 15, protocol 297) is
  refused.
- **Maps, mods and settings.** Saved `.kmp` maps, generated-map recipes,
  `ModCache/` downloads, pack folders and settings files carry over. The
  settings of a 0.9.3 profile load unchanged.

No new runtime dependencies or retail assets are included (SQLite is gone). See
the [release validation report](release-0.9.4-validation.md).
