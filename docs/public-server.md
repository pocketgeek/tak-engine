# Public server deployment

The development server includes native TLS, authentication, resource admission
limits and an optional Linux service sandbox. These reduce risk; they do not
constitute an independent security audit or protection against a volumetric DDoS.
The released 0.7.20 binaries predate these changes.

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

Players enter **`tls://your-server.example:7677`** in the multiplayer server
field. TLS verifies both the certificate chain and the DNS name/IP address.
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
Internet deployment setting. Existing bare hostnames select plaintext; they
must be changed to `tls://...` when the server switches to TLS. Old clients
cannot connect to a TLS listener. The gameplay protocol remains 212.

The login exchange alone is not session encryption. Do not expose the old
plaintext port alongside TLS as a compatibility fallback.

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
player commands. Ownership is stamped by the server and checked by the simulation.

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
