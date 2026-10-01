# Public server hardening validation — 2026-10-01

Implementation: `61c800a`, `3aaecfe`, `d43b04b`, and `c40473e` on main.
This is post-0.7.20 development work; no release or live deployment was made.

## Checks

- Release full suite: **159/159 passed** after the command-coordinate validation change.
- Optimized Debug full suite: **167/167 passed** after the command-coordinate validation change.
- Final focused Clang network/input suite: **5/5 passed**.
- Native TLS tests cover trusted, untrusted, expired and wrong-host certificates,
  plaintext rejection, framed transfer with backpressure, and abrupt peer closure.
- Real-server abuse tests cover public benchmark rejection, room/account limits,
  upload reservation, closed registration, request floods, invalid configuration,
  TLS authentication, and an 8 MiB paced TLS upload without disconnecting its sender.
- Command-validation tests reject malformed batches, unknown command types,
  NaN/infinity, and coordinates outside signed 16.16; every valid command kind
  remains accepted. Production batches above the UI maximum of ten are rejected.
  These tests also passed under AddressSanitizer/LeakSanitizer.
- After adding the production-count bound, focused Release checks passed **2/2**,
  optimized Debug network/command checks **3/3**, Clang command checks **1/1**,
  and ASan/LSan command checks passed.
- Before the command-validation change, the focused Clang suite passed
  **6/6**, and the ASan/LSan network/storage suite passed **6/6**.
- `systemd-analyze verify packaging/systemd/takserver.service` passed.
  The service sandbox was not installed or exercised as a live service.
- Linux Release `takserver` has no dynamic OpenSSL dependency.

## Native platform CI

The static TLS integration passed both macOS architectures in
[run 36910306663](https://github.com/pocketgeek/tak-engine/actions/runs/36910306663).
Windows and subsequent main builds are tracked by the repository’s
[Actions page](https://github.com/pocketgeek/tak-engine/actions). A passing Linux
run alone does not establish Windows compatibility.

## Remote encrypted multiplayer

Isolated authenticated TLS servers were run on **tak.pgnet.us** and
**vpn3.pgnet.us**, using temporary test certificates and accounts. The Ubuntu
GCC 13 server and local GCC 16 client ran Aibel’s Seaport for 900 ticks in both
Standard and Crusades balance. All four runs finished without a desync:

```
tick=900 hash=905baa442423c76c units=4 err=none end=timelimit
```

These were short compatibility smokes, not long-duration stress matches. They
used revision `3aaecfe`; later changes bound error-message forwarding, validate
player commands, and repair the Windows ARM dependency build. Temporary remote
servers and their files were removed; the live public services were untouched.

## Operational boundary

See [public-server.md](public-server.md) for configuration and remaining risks.
An administrator still needs to install valid certificates, configure the
service and its filesystem quota, and update players to a `tls://` address.
Application limits are not a substitute for host isolation, backups, or upstream
DDoS protection. This work is not an independent security audit or exhaustive
asset-parser fuzzing campaign.
