# What kingdoms.mmz contains

Classification: CONFIRMED container/content observations; updater role is
RECONSTRUCTED from its declarations and the companion DLL's exported API.
Provenance: the two fingerprinted patch payloads in [sources.md](sources.md).

Each `kingdoms.mmz` is a ZIP containing one `Kingdoms.mmm` member. Neither
manifest is a persistent campaign database or a territory rule script.

| Payload | Decoded bytes | Decoded SHA-256 | Package version declaration |
|---|---:|---|---|
| Standard | 19475 | `8f1c55dbbc62cf38909f2b03ee0acf28b4955497a87bb05b4325bd94bcd15aa6` | 30BA |
| Crusades | 19884 | `3bf5a7c7f88849bc9d2d459cf6ba917c7b02c3ff58beb48f8f534f7409e1614b` | 30BB |

The decoded format identifies Maia version 2.0 and declares Win32 package
metadata, HTTP/FTP update locators, bundles, resource paths, byte sizes, MD5
values and bootstrap version relationships. The companion `bymaia.dll` exports
package update, source URI, package version, local verification and restart
configuration APIs. Export presence does not prove a specific call was used.

The differences include a 30BB bootstrap bundle, changed advertised hashes and
sizes for selected existing resources, an extra profile HTML resource, a
readme resource and a different signature. Signature presence has not been
cryptographically verified. Historical locator declarations do not mean those
servers remain available or that any downloads were executed.

Notably the Crusades manifest advertises a 6923-byte v3readme while the extracted
payload's file is 7113 bytes. The manifests therefore cannot be treated as an
exact inventory of the payload or proof of a completed installation. Installer
conditions and update precedence still need separate analysis.

The metagame bundle lists Darien.def and both JJE tables. Searches of both
manifests found no MetaMask, Borders or HonorMap resource names. These images
are now independently verified in the fingerprinted GOG distribution and match
the current local installation. The original CD cabinet also lacks these loose
images; the historical Boneyards delivery route remains unresolved. See
[distribution provenance](distribution-provenance.md). Authentic artwork and
its native hit-test use still do not establish authoritative territory adjacency.

To inspect a user-supplied file offline, Python's standard `zipfile.ZipFile` can
list members and read `Kingdoms.mmm` without extracting paths or executing
anything. Original manifest text remains outside Git; committed inventories
already fingerprint the compressed files.

## Earlier updater manifests from the FTP survey

The [expanded FTP survey](sources.md#expanded-cavedog-ftp-survey) adds thirteen
decoded MMZ members from earlier TA and Kingdoms packages. Metadata and hashes
are in [the survey inventory](inventories/cavedog-ftp-survey.json). The earlier
Kingdoms manifests advertise versions 20, 20BD and 20BH, as well as bootstrap
packages; `tak30BAupdate.EXE` supplies the already recorded 30BA manifest bytes.

Their historical Kingdoms update locators include:

```text
ftp://ftp.boneyards.net/boneyards/tak/updates
ftp://ftp2.boneyards.net:8889/boneyards/tak/updates
http://update.boneyards.net/boneyards/tak/updates
http://update2.boneyards.net:8887/boneyards/tak/updates
```

These are historical declarations, not verified active services. Earlier TA
manifests use the analogous `/boneyards/ta/updates` path; the games' campaign
data must remain separate.

`TAKBY20c.exe` already advertises `metagame.byz`, with Darien.def, PreInit.jje
and the allegiance shield images. The 20BH/30BA manifests advertise more of
the familiar metagame art and wdhit.jje. No inspected earlier Kingdoms manifest
adds MetaMask, Borders or HonorMap resource names or a campaign server package.
The original updater bundle files themselves are not present under their
advertised names in the inspected 367-file Cavedog mirror directory.

`byserver.byz` and bootstrap `byserver_f.byz` target `Boneyards/server.byd`.
The extracted 41-byte server.byd is a tab-separated directory entry specifying
the bypacific host, boneyards.net domain and a beta-region label. It is not
server source, an executable, a campaign database or a rule script. Likewise,
the 30BA manifest's `servers` block contains a support URL; its name is not
evidence of bundled server code.

A preserved copy of the separate Boneyards update tree or its `metagame.byz`
versions remains a concrete archival lead. Merely finding a modern server at
one of these domains would not authenticate original campaign rules. Neither
the locators nor the newly found older definition justify inferring missing
territory connections or live initial owners.
