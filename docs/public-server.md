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
listener. The gameplay protocol remains 212.

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

Initial issuance must succeed before the game listener starts. Subsequent starts
reuse a valid saved certificate. Renewal runs in a background worker with bounded
requests, nonce retries, persistent exponential backoff and CA `Retry-After`
handling. It renews when about one-third of certificate lifetime remains (half
for certificates shorter than ten days), without assuming a fixed 90-day lifetime.
ACME Renewal Information (ARI) scheduling is not yet implemented. A successful
renewal atomically saves the certificate/key bundle and switches new connections
to it; existing games keep their established TLS sessions. Renewal failures keep
the previous certificate and log the problem. An expired certificate will still
be rejected by clients, so administrators must monitor renewal failures.

On Linux, the listener needs permission to bind port 80. The optional
[systemd ACME drop-in](../packaging/systemd/takserver-acme.conf) grants only
`CAP_NET_BIND_SERVICE` while retaining the main service sandbox. Copy it to
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

All player-command batches are checked before enqueueing: invalid command
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

## Linux service isolation

[packaging/systemd/takserver.service](../packaging/systemd/takserver.service)
is an opt-in template; packages do not enable a public server automatically.
It uses a dynamic unprivileged user, private state directory, read-only system
and home isolation, no capabilities, restricted system calls and address
families, no executable writable memory, bounded logs, and process limits.
The TLS key is passed through systemd credentials rather than made world-readable.

1. Install retail data under `/srv/tak-data`, readable by the service. Keep it
   owned by an administrator. The service writes maps under its state directory.
2. Install a valid certificate chain at `/etc/takserver/fullchain.pem` and its
   private key at `/etc/takserver/private.pem`. Keep the key readable only by root;
   systemd supplies its private copy to the service.
3. Install the template in `/etc/systemd/system/takserver.service`; review paths,
   the 8 GiB process memory limit and the game limits for the host.
4. Run `systemd-analyze verify /etc/systemd/system/takserver.service`, then
   `systemctl daemon-reload` and `systemctl enable --now takserver`.
5. Allow only the intended TCP game port (7677 by default). Keep SSH/admin access
   separately restricted. Watch `journalctl -u takserver` for quota and certificate
   problems. Use a filesystem quota for the state directory and backups; the
   application does not cap the Crusades database or the system journal's total size.

On Windows/macOS, use a dedicated standard service account, restrict data/key
permissions, and expose only the game port. The supplied systemd sandbox is
Linux-specific; it is not installed or claimed to apply on those platforms.

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
