# Public server deployment

The server includes native TLS, authentication, resource admission
limits and an optional Linux service sandbox. These reduce risk; they do not
constitute an independent security audit or protection against a volumetric DDoS.
These features ship in 0.7.21; 0.7.20 binaries predate them.

## Encrypted connections

A listener reachable off the machine requires a certificate and private key:

```sh
takserver --data /srv/tak-data \
  --accounts /var/lib/takserver/accounts.conf \
  --map-cache-dir /var/lib/takserver/maps \
  --replaydir /var/lib/takserver/replays \
  --tls-cert /etc/takserver/fullchain.pem \
  --tls-key /etc/takserver/private.pem
```

Create the writable directories and restrict them to the server account before
starting. The certificate must cover the DNS name players use and chain to a CA
trusted by their operating system. The certificate file includes intermediate
certificates. Renew it before expiry and restart the server to load the renewal;
there is no live certificate reload.

Players enter just **`your-server.example`** in the multiplayer server field.
The client automatically uses TLS and port **7677**. An optional `:port` supports
servers on other ports; existing `tls://` addresses also work. TLS verifies both the certificate chain and the DNS name/IP address.
An invalid certificate fails the connection; there is no plaintext fallback.
A TLS listener accepts only TLS connections. TLS 1.2 or later protects the whole
session, including registration, login, game passwords, chat and commands.
Account authentication is still required by default.

OpenSSL is compiled into the binaries on Linux, Windows x64/ARM64 and macOS
x64/ARM64. No OpenSSL DLL/shared library is shipped or required. Windows and
macOS system trust anchors are imported; Linux uses system CA bundles. An
administrator may configure OpenSSL's standard `SSL_CERT_FILE`/`SSL_CERT_DIR`
for a private CA. There is deliberately no “ignore certificate errors” option.

`--local` binds loopback and permits plaintext for a private local client or
an encrypted tunnel. `--allow-plaintext` explicitly permits an unencrypted
external listener for a trusted LAN/test environment. It is **not** a public
Internet deployment setting. To use one from the multiplayer menu, explicitly
enter `tcp://hostname` (and an optional `:port`). Bare menu hostnames always use
TLS, with no plaintext fallback. Automatically launched single-player servers
keep their private loopback connection. Debug command-line harness connections
retain their explicit transport behavior. Old clients cannot connect to a TLS
listener. Version 0.7.27 uses gameplay protocol 238; update clients and servers together.

The login exchange alone is not session encryption. Do not expose the old
plaintext port alongside TLS as a compatibility fallback.

## Built-in Let’s Encrypt certificates

`takserver` can obtain and renew its own certificate without Certbot or another
installed ACME program:

```sh
takserver --data /srv/tak-data \
  --accounts /var/lib/takserver/accounts.conf \
  --map-cache-dir /var/lib/takserver/maps \
  --replaydir /var/lib/takserver/replays \
  --acme-domain tak.example.org --acme-agree-tos \
  --acme-state /var/lib/takserver/acme
```

Replace `tak.example.org` with a DNS hostname you control. Its A/AAAA records
must reach this machine, including IPv6 if published. Allow incoming TCP **80**
through the firewall/router and outgoing HTTPS **443** to the CA. The game still
uses TCP **7677**. The server opens an HTTP listener only while an HTTP-01
challenge is pending, serves only its exact challenge token, and closes it after
validation or failure. It does not run a general website, redirect service, or
permanent port-80 listener, and does not change your firewall automatically.
If another service already owns port 80, issuance fails cleanly; TAK will not stop
that service or fall back to plaintext. Wildcards and IP certificates are not
supported by this implementation.

`--acme-agree-tos` records the operator’s agreement to the CA subscriber terms.
`--acme-email you@example.org` optionally supplies an account contact. The state
directory defaults to `takserver-acme`; use a persistent absolute directory for a
service. Keep it private and back it up: it contains the account key, certificate
key, certificate chain and retry state. A process lease prevents two servers from
managing the same directory. A directory is tied to its hostname and CA; use
separate directories for different hosts and staging.

Initial issuance and renewal run in a background worker. Until a certificate is
available, the game listener refuses connections; it never falls back to plaintext.
Subsequent starts reuse a valid saved certificate. Transient initial failures and
persisted backoffs leave the service running, so the packaged systemd restart
limit cannot prevent eventual recovery. Shutdown interrupts retry waits promptly;
do not delete retry state to bypass a CA backoff. The worker uses bounded
requests, nonce retries, persistent exponential backoff and CA `Retry-After`
handling. It renews when about one-third of certificate lifetime remains (half
for certificates shorter than ten days), without assuming a fixed 90-day lifetime.
The temporary HTTP-01 listener allows 32 connections globally and four per
normalized source address (IPv4 and IPv4-mapped IPv6 share a limit). Its three-second
deadline and 8 KiB request limit also apply to incomplete requests.
ACME Renewal Information (ARI) scheduling is not yet implemented. A successful
renewal atomically saves the certificate/key bundle and switches new connections
to it; existing games keep their established TLS sessions. Renewal failures keep
the previous certificate and log the problem. An expired certificate will still
be rejected by clients, so administrators must monitor renewal failures.

On Linux, the listener needs permission to bind port 80. The optional
[systemd ACME drop-in](../packaging/systemd/takserver-acme.conf) grants only
`CAP_NET_BIND_SERVICE` while retaining the main service sandbox. Linux packages
install the service in `/usr/lib/systemd/system/takserver.service` and the template
plus setup notes in `/usr/share/doc/tak-engine/systemd/`. Neither the service nor
ACME is enabled automatically. Copy the `takserver-acme.conf` template to
`/etc/systemd/system/takserver.service.d/acme.conf`, replace its hostname, and
review the paths before restarting. It replaces the manual certificate options
and credential entry. Do not run the game server as root just to bind port 80.
On Windows/macOS, configure the service account and firewall so the process can
bind TCP 80. Platform service installers are not supplied for those systems.
State uses private owner ACLs on Windows and owner-only Unix
permissions. No new DLL/shared-library dependency is added; OpenSSL is static and
the vendored JSON parser is header-only (see `ACME-LICENSES.txt`).

Use `--acme-staging` with a **separate state directory** to exercise Let’s Encrypt’s
test service before production. Staging certificates are not trusted by ordinary
clients. For local development, `acme_test_driver` and
`tools/acme_pebble_test.py` exercise issuance, saved-state reuse, background
renewal, occupied-port failure, retained certificates, backoff and listener cleanup
against [Pebble](https://github.com/letsencrypt/pebble). The test driver’s custom
CA endpoint and challenge port are not production server options. The native
`acme` CTest covers responder bounds, exact paths, slow peers and cleanup.

For the integration script, run Pebble with its default port 5002 for HTTP
validation, `PEBBLE_AUTHZREUSE=0`, and DNS/hosts mapping `acme.tak.test` to this
machine. Point `--ca` at Pebble’s API root certificate:

```sh
python3 tools/acme_pebble_test.py --driver build-o2/acme_test_driver \
  --ca /path/to/pebble.minica.pem
```

The optional integration harness needs Python’s `cryptography` package to create
an already-due fixture; the game/server and native CTests do not need Python
cryptography, Pebble, or a container runtime. Validation on 2026-10-01 passed the
169-test optimized Debug suite, six focused Release and Clang checks, and both
native and end-to-end ACME checks under AddressSanitizer/LeakSanitizer. Pebble was
configured to reject 50% of nonces to exercise retry handling. The TLS regression
also replaces the server context while an established connection transfers data.
These checks do not issue a production certificate or alter any live service.

## Resource controls

| Option | Default | Scope |
|---|---:|---|
| `--max-games` | 16 | Lobby and running rooms combined |
| `--max-running-games` | 4 | Concurrent referee simulations |
| `--max-accounts` | 10,000 | New registrations; existing logins remain available |
| `--map-memory-mib` | 512 | Reserved uploads plus retained room packages; includes canonical and decoded file bytes |
| `--map-storage-mib` | 4,096 | Download cache and saved generated maps together |
| `--replay-memory-mib` | 256 | Estimated replay history memory per game |
| `--replay-storage-mib` | 4,096 | Each replay archive directory |

Limits must be positive; invalid values fail startup. Game count limits may be
raised to 64, accounts to 100,000, and memory/storage limits to 65,536 MiB.
Choose limits appropriate to the host. These are application budgets, not a
replacement for a process memory limit or filesystem quota: simulation state,
transient decoding/serialization, TLS and allocator overhead also use memory.

Map uploads reserve their advertised size before receiving data and expire
after 30 seconds without progress or five minutes total. Each package remains
limited to 256 MiB. New disk writes are rejected at quota; old maps/replays are
not silently deleted. Existing downloaded maps remain selectable. Use
`--map-cache-dir` to keep writable map state outside the retail install.

Ordinary lobby/chat/setting requests have per-connection, account, IP and global
work budgets. Expensive creation/start/map-offer requests cost more than small
queries; map chunks have separate byte budgets. Keepalive has a connection
budget so one abusive connection cannot disconnect its healthy NAT neighbours.
Authentication and Crusades queries retain their own throttles. Account-file
rewrites are also limited globally. New connections are limited before TLS
allocation (32 per IP and 128 globally per second). Upload reads apply
backpressure at 4 MiB/s per connection and 16 MiB/s globally; fast legitimate
senders are paced rather than disconnected.

Map and override validation uses one background worker and at most two waiting
jobs. Admission reserves twice the advertised package size for canonical and
extracted bytes, including jobs whose sender disconnects. Cached file sizes are
checked before allocation and must match the offer; uploaded lengths and final
admission are checked again. On a cache miss, even an installed map is requested
from the host instead of rebuilding it on the network loop. This can add a first-use
upload but preserves the existing protocol. Validation results are discarded after
the host leaves, changes rooms or changes the relevant settings.
Cancellation uses a shared atomic flag whose lifetime is independent of the
connection, including on the macOS 14 SDK; workers never dereference a departed
client to check cancellation.

TDF parsing preserves case-insensitive, last-definition lookup and duplicate
section order, using an index instead of scanning earlier entries. Limits are
65,536 sections, 262,144 nodes (sections plus assignments), depth 32, and a
conservative 64 MiB allocation-accounting allowance. Map feature definitions share
these limits across the package and have an additional 8 MiB source-text limit.
Oversized custom content is rejected. The map-memory setting is a package admission
budget, not a total process RSS limit: the single worker also uses bounded parser,
geometry and cache-writing scratch memory. Keep systemd's MemoryMax above the
package budget plus simulation and validation working memory.

Commands have one-second byte and count budgets: 4 MiB/65,536 commands per connection
and account (source address for unauthenticated private clients), and
32 MiB/524,288 commands globally. These exceed normal client command credits,
including large battles at 4x speed. Exhaustion applies receive backpressure;
batches that do not fit the 512-command queue are dropped whole before decoding.
Overflow summaries are limited to one per connection per five seconds and four
per second globally. Accepted batches are parsed once, with no partial enqueue.

All admitted player-command batches are checked before enqueueing: invalid command
types, truncated/trailing data, non-finite coordinates and coordinates outside
the simulation’s signed 16.16 range are rejected. Spectators cannot submit
player commands. Production count requests are limited to the client’s maximum
batch of ten, preventing a forged count from allocating billions of queue entries.
Ownership is stamped by the server and checked by the simulation.

Public games cannot enable benchmark/stress workloads or launch local campaign
missions. Private `--local --no-auth` games retain those features. Debug builds
have an explicit `--allow-benchmarks` override for isolated performance harnesses;
Release builds do not accept it. This does not disable authenticated Darien
Crusades battles or verified authored map scenarios.

A game that exhausts its replay-memory budget is stopped and its clients are
disconnected with a resource-limit error. History is not silently truncated,
which would invalidate reconnects and replays. Campaign resource failures do
not award victory credit. Ordinary replay saving is skipped when its disk quota
is full; a campaign outcome whose required replay cannot be persisted is handled
as a no-credit abort.

`--closed-registration` allows existing accounts to log in while refusing new
ones. It can be enabled after inviting the initial community. Keep the account
file private and back it up together with any Crusades database and replay
archive. Do not use game-account passwords on unrelated services.

## Linux service setup

Linux packages include these files:

| File | Purpose |
| --- | --- |
| `/usr/lib/systemd/system/takserver.service` | Packaged service unit |
| `/usr/share/doc/tak-engine/systemd/takserver-acme.conf` | Optional ACME override template |
| `/usr/share/doc/tak-engine/systemd/takserver-settings.conf` | Optional server settings and resource limits |
| `/usr/share/doc/tak-engine/systemd/README.md` | Installed setup notes |

Installing the package does **not** start or enable the server. The ACME template
is also inactive until copied into the service's override directory and edited.
Choose either built-in ACME or an existing certificate below before starting.

For a packaged installation, the unit is already installed: **do not copy another
unit into `/etc/systemd/system/`**. Follow the numbered steps below. Keep custom
settings in `/etc/systemd/system/takserver.service.d/` so package upgrades can
update the vendor unit without replacing your configuration. A concise
[packaged ACME walkthrough](../README.md#linux-server-setup) is also in the README.

### 1. Install the retail data

The default service expects retail files in `/srv/tak-data`. Replace the source
path in this example with your actual retail installation directory:

```sh
sudo mkdir -p /srv/tak-data
sudo cp -a /path/to/retail-install/. /srv/tak-data/
sudo chown -R root:root /srv/tak-data
sudo chmod -R a+rX /srv/tak-data
```

The directory must contain the actual game archives and other retail data, not
just an empty folder. Use matching base game files on the server and clients.
The service has `ProtectHome=yes`: data under `/home` is hidden, even if it is
world-readable. A symlink from `/srv/tak-data` into `/home` does not bypass that
sandbox. Copy the data outside `/home` as shown above.

Systemd creates and manages `/var/lib/takserver` for writable accounts, downloaded
maps, replays and ACME state. You do not need to create a permanent `takserver`
user or make that state directory world-writable.

### 2. Choose a certificate mode

**Built-in Let's Encrypt:** copy the supplied template and edit it:

```sh
sudo mkdir -p /etc/systemd/system/takserver.service.d
sudo cp -i /usr/share/doc/tak-engine/systemd/takserver-acme.conf \
  /etc/systemd/system/takserver.service.d/acme.conf
sudoedit /etc/systemd/system/takserver.service.d/acme.conf
```

If `acme.conf` already exists, keep your existing file and edit it instead of
overwriting it. The `cp -i` command prompts before replacing a file.

**Replace `YOUR.SERVER.NAME` with your real public DNS hostname**, such as
`tak.example.org`, without a URL scheme or port number. The complete drop-in
then looks like this:

```ini
[Service]
LoadCredential=
ExecStart=
ExecStart=/usr/bin/takserver \
    --data ${TAK_SERVER_DATA} \
    --accounts ${TAK_SERVER_ACCOUNTS} \
    --map-cache-dir ${TAK_SERVER_MAP_CACHE} \
    --replaydir ${TAK_SERVER_REPLAYS} \
    --port ${TAK_SERVER_PORT} \
    --max-games ${TAK_SERVER_MAX_GAMES} \
    --max-running-games ${TAK_SERVER_MAX_RUNNING_GAMES} \
    --max-accounts ${TAK_SERVER_MAX_ACCOUNTS} \
    --map-memory-mib ${TAK_SERVER_MAP_MEMORY_MIB} \
    --map-storage-mib ${TAK_SERVER_MAP_STORAGE_MIB} \
    --replay-memory-mib ${TAK_SERVER_REPLAY_MEMORY_MIB} \
    --replay-storage-mib ${TAK_SERVER_REPLAY_STORAGE_MIB} \
    --acme-domain tak.example.org \
    --acme-agree-tos \
    --acme-state /var/lib/takserver/acme \
    $TAK_SERVER_EXTRA_ARGS
CapabilityBoundingSet=CAP_NET_BIND_SERVICE
AmbientCapabilities=CAP_NET_BIND_SERVICE
```

The empty `LoadCredential=` and `ExecStart=` entries remove the base unit's manual
TLS configuration before supplying the ACME command. Keep them. Use the settings drop-in below to change the data path or limits. Check the
executable path if you installed elsewhere. `--acme-agree-tos` records
your acceptance of the CA subscriber agreement.

Point the hostname's A/AAAA records at the server, allow inbound TCP **80** and
**7677**, and allow outbound HTTPS **443**. Port 80 must be available for issuance
and renewal; takserver listens there only during validation. Certificates and
keys are managed under `/var/lib/takserver/acme`. You do not need manual
`/etc/takserver` certificate files in this mode. See
[built-in certificate handling](#built-in-lets-encrypt-certificates) for renewal
and port-conflict behavior.

**Existing certificate:** leave the ACME drop-in uninstalled. Install your
certificate chain and private key at the paths used by the base unit:

```sh
sudo install -d -m 755 /etc/takserver
sudo install -m 644 /path/to/fullchain.pem /etc/takserver/fullchain.pem
sudo install -m 600 /path/to/private.pem /etc/takserver/private.pem
```

The certificate must cover the hostname players enter. Systemd reads the
root-only key and passes it privately to the dynamic service user. Allow inbound
TCP **7677**; takserver does not use port 80 in this mode. Renew the certificate
externally and restart the service after replacing its files.

### 3. Start and check the service

```sh
sudo systemctl daemon-reload
sudo systemd-analyze verify takserver.service
sudo systemctl enable --now takserver
sudo systemctl status takserver --no-pager
sudo journalctl -u takserver -f
```

If the service was already running when you changed its configuration, run
`sudo systemctl restart takserver` after `daemon-reload`; `enable --now` does not
restart an already active service. `sudo systemctl cat takserver` shows the unit
and its drop-ins, which is useful for checking the effective command and hostname.

For first-time ACME setup, wait for `ACME: certificate installed` before connecting.
The listener can be running while issuance or a persisted backoff is still pending;
it refuses game connections until a certificate is available. A valid saved
certificate is usable immediately. Players enter just the configured hostname
in the multiplayer connection screen.

If a connection is rejected, inspect the journal for the actual reason. A retail
game-data mismatch can mean the `--data` directory is missing, inaccessible or
contains different base files. Confirm that path exists outside the hidden home
directories. For ACME failures, check the hostname, DNS records, firewall and
whether another process owns port 80.

### Querying server status

Run this on the machine hosting the server:

```sh
takserver --status
```

For a nondefault game port, use `takserver --status --port 7678`. A successful
query writes only JSON to standard output and exits with status 0:

```json
{
  "running_games": 2,
  "lobby_games": 1,
  "connected_clients": 7
}
```

Counts include private games and enabled Crusades games. `running_games`
includes started games that are loading or paused; `lobby_games` counts rooms
that have not started. `connected_clients` counts established player, spectator,
and game-browser sessions. It excludes unfinished handshakes/logins, disconnected
players retained for rejoining, AI players, and status queries themselves.

No `--data`, account, or certificate arguments are needed. The server binds a
small status listener to **127.0.0.1 UDP** on the game TCP port's numeric value
(7677 by default). This also works with manual TLS and while ACME is waiting for
a certificate; it reports activity, not certificate readiness. The UDP port must
be free when the server starts. Do not forward or open it to the Internet: this
is a local monitoring interface and requires no change to the packaged service
sandbox. For remote monitoring, run the command through SSH.

The query requires an updated running server. Failure or a three-second timeout
returns a nonzero exit status with a diagnostic on standard error and no JSON
on standard output. Status processing is bounded to 32 datagrams per second,
including invalid requests, with at most eight handled per network-loop pass.
Poll about once per second; excessive local traffic can cause query timeouts.
Status does not modify game state or change the gameplay network protocol.

### Changing server defaults

The packaged `takserver-settings.conf` works with either manual TLS or the current
ACME template. You can change paths, port, game/account limits, storage budgets
and systemd resource limits without rewriting `ExecStart`.

1. Copy the settings template and open your local copy:

   ```sh
   sudo mkdir -p /etc/systemd/system/takserver.service.d
   sudo cp -i /usr/share/doc/tak-engine/systemd/takserver-settings.conf \
     /etc/systemd/system/takserver.service.d/settings.conf
   sudoedit /etc/systemd/system/takserver.service.d/settings.conf
   ```

   Keep an existing `settings.conf` rather than overwriting your settings. The
   template starts with the defaults listed under [resource controls](#resource-controls).

2. Change the values you need. For example, these entries allow up to 32 rooms,
   eight running games and 20,000 accounts, with a 16 GiB process memory ceiling:

   ```ini
   [Service]
   Environment="TAK_SERVER_MAX_GAMES=32"
   Environment="TAK_SERVER_MAX_RUNNING_GAMES=8"
   Environment="TAK_SERVER_MAX_ACCOUNTS=20000"
   MemoryMax=16G
   ```

   The full template also includes `TAK_SERVER_PORT`, `TAK_SERVER_DATA`,
   `TAK_SERVER_ACCOUNTS`, `TAK_SERVER_MAP_CACHE`, `TAK_SERVER_REPLAYS`,
   `TAK_SERVER_MAP_MEMORY_MIB`, `TAK_SERVER_MAP_STORAGE_MIB`,
   `TAK_SERVER_REPLAY_MEMORY_MIB`, and `TAK_SERVER_REPLAY_STORAGE_MIB`.
   Game-count limits must be 1–64; account and budget limits must be positive.
   Memory/storage budgets are in MiB. Changing `MemoryMax` alone does not change
   the application's limits. If changing the game port, update your firewall and
   tell players to enter `hostname:port`.

   Keep writable paths under `/var/lib/takserver`; the sandbox makes most other
   locations read-only and hides home directories. Data paths may contain spaces:
   keep each complete `Environment="NAME=value"` assignment quoted as shown.

   To allow only existing accounts, change the template's empty extra-arguments
   entry to:

   ```ini
   Environment="TAK_SERVER_EXTRA_ARGS=--closed-registration"
   ```

   Set it back to `Environment="TAK_SERVER_EXTRA_ARGS="` to allow registration.
   Extra flags are split into arguments by systemd, not run through a shell.
   Do not put passwords or private keys in environment settings. TLS/ACME choices
   remain in the base unit or `acme.conf`; do not duplicate those options here.

3. Apply and inspect the configuration:

   ```sh
   sudo systemctl daemon-reload
   sudo systemd-analyze verify takserver.service
   sudo systemctl restart takserver
   sudo systemctl cat takserver
   sudo systemctl show takserver -p Environment -p MemoryMax -p TasksMax -p LimitNOFILE
   sudo journalctl -u takserver -n 50 --no-pager
   ```

   Restarting disconnects active games, so apply changes between matches.
   The variables are expanded by systemd into normal command-line arguments;
   this does not add environment-variable handling to the Release server.

**Upgrading an existing setup:** an older `acme.conf` or a full service copy under
`/etc/systemd/system/takserver.service` may contain a hard-coded `ExecStart`.
Environment settings will not change that command. Use `systemctl cat takserver`
to inspect it, then merge the current packaged template's variable-based command
into your local configuration, preserving your hostname and custom settings.
Do not overwrite an existing ACME file with `YOUR.SERVER.NAME` still in it.

### Source installations and older packages

If installing from source, or using an older package without these files, use
[the service unit](../packaging/systemd/takserver.service) and
[ACME template](../packaging/systemd/takserver-acme.conf) from this checkout.
From the checkout root, install the service with:

```sh
sudo install -m 644 packaging/systemd/takserver.service /etc/systemd/system/takserver.service
```

A unit in `/etc/systemd/system/` takes precedence over the packaged unit. For
packaged installations, keep custom settings in drop-ins rather than copying or
editing the vendor unit.

### Service isolation and maintenance

The unit uses a dynamic unprivileged user, private state directory, read-only
system and home isolation, restricted system calls and address families, no
executable writable memory, bounded logs, and process limits. The base unit has
no capabilities; the ACME override grants only `CAP_NET_BIND_SERVICE`. Review the
8 GiB process memory limit and the [game limits](#resource-controls) for your host.
Do not run takserver as root to make certificate issuance work.

Keep SSH/admin access separately restricted. Back up private state and monitor
certificate and quota errors. Use filesystem quotas if needed; application
limits do not cap the Crusades database or the system journal's total size.
Package updates replace the vendor unit and template, not your edited drop-in.

On Windows/macOS, use a dedicated standard service account, restrict data/key
permissions, and expose the game port plus port 80 when using ACME. The supplied
systemd service and sandbox are Linux-specific.

## Validation and remaining limits

See the [2026-10-01 validation report](public-server-validation-2026-10-01.md)
for local suites, remote TLS matches, and the deployment boundary.

`tls_test` checks encrypted framing/backpressure, valid connections, wrong host,
untrusted/expired certificates, abrupt peer closure and plaintext rejection.
`server_public_test.py` exercises real-server admission, map reservation,
registration closure, floods, invalid configuration, TLS login and ordinary
lobby traffic. `server_commands_test` checks malformed command batches and
unsafe coordinates. Existing authentication, campaign, map-transfer and multiplayer
regressions remain required. Native platform CI runs the TLS test and verifies
that executable imports contain no new non-system dynamic libraries.

Remaining risks include malicious asset-parser inputs not covered by these
tests, CPU-heavy but valid maps/scenarios, distributed connection exhaustion,
and attacks exceeding the host/network capacity. Map admission and room caps
bound work but do not isolate each referee into its own process. The TLS client
checks chains, hostnames and expiry; online certificate-revocation checking is
not implemented. Keep OpenSSL and operating-system trust stores updated.

Implementation references: [OpenSSL hostname verification](https://docs.openssl.org/3.6/man3/SSL_set1_host/),
[OpenSSL protocol minimum](https://docs.openssl.org/master/man3/SSL_CTX_set_min_proto_version/),
and [systemd execution sandbox](https://github.com/systemd/systemd/blob/main/man/systemd.exec.xml).

### Focused untrusted-input regressions

Run the parser, worker, command, package and local server tests with:

```sh
ctest --test-dir build --output-on-failure -R '^(tdf_limits|validationworker|server_commands|server_command_flood|map_transfer|map_transfer_data|override_packs|override_transfer|server_public|acme)$'
```

Data-backed tests require CMake's `TAK_TEST_DATA` to point to a retail installation.
The additional local HTTPS ACME fixture requires Python `cryptography` (test-only):

```sh
python3 tools/acme_recovery_test.py --driver build/acme_test_driver --tls-tool build/tls_test
```

It deliberately waits through the real initial CA backoff, then verifies successful
issuance without restarting and recovery from a future persisted deadline. It uses
only loopback endpoints and temporary keys; it does not contact Let's Encrypt.
The C++ ACME test also covers long persisted backoff, prompt shutdown, strict paths
and challenge availability while another source holds incomplete connections.
